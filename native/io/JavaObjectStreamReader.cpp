#include "io/JavaObjectStreamReader.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

#include "io/DecodeError.h"

namespace {

constexpr uint16_t STREAM_MAGIC = 0xaced;
constexpr uint16_t STREAM_VERSION = 5;
constexpr uint8_t TC_NULL = 0x70;
constexpr uint8_t TC_REFERENCE = 0x71;
constexpr uint8_t TC_CLASSDESC = 0x72;
constexpr uint8_t TC_OBJECT = 0x73;
constexpr uint8_t TC_STRING = 0x74;
constexpr uint8_t TC_ARRAY = 0x75;
constexpr uint8_t TC_CLASS = 0x76;
constexpr uint8_t TC_BLOCKDATA = 0x77;
constexpr uint8_t TC_ENDBLOCKDATA = 0x78;
constexpr uint8_t TC_RESET = 0x79;
constexpr uint8_t TC_BLOCKDATALONG = 0x7a;
constexpr uint8_t TC_LONGSTRING = 0x7c;
constexpr uint8_t TC_PROXYCLASSDESC = 0x7d;

constexpr uint8_t SC_WRITE_METHOD = 0x01;
constexpr uint8_t SC_SERIALIZABLE = 0x02;
constexpr uint8_t SC_EXTERNALIZABLE = 0x04;
constexpr uint8_t SC_BLOCK_DATA = 0x08;
constexpr int32_t BASE_WIRE_HANDLE = 0x7e0000;

[[noreturn]] void fail(DecodeErrorCode code, const char* message) {
    throw DecodeError(code, message);
}

class NestingGuard {
public:
    explicit NestingGuard(size_t& depth) : mDepth(depth) {
        if (mDepth >= JavaObjectStreamReader::MAX_NESTING_DEPTH) {
            fail(DecodeErrorCode::LimitExceeded,
                    "Java serialization nesting depth exceeds the safety limit");
        }
        mDepth++;
    }

    ~NestingGuard() {
        mDepth--;
    }

    NestingGuard(const NestingGuard&) = delete;
    NestingGuard& operator=(const NestingGuard&) = delete;

private:
    size_t& mDepth;
};

bool isPrimitiveType(char typeCode) {
    switch (typeCode) {
        case 'B':
        case 'C':
        case 'D':
        case 'F':
        case 'I':
        case 'J':
        case 'S':
        case 'Z':
            return true;
        default:
            return false;
    }
}

size_t primitiveTypeSize(char typeCode) {
    switch (typeCode) {
        case 'B':
        case 'Z':
            return 1;
        case 'C':
        case 'S':
            return 2;
        case 'F':
        case 'I':
            return 4;
        case 'D':
        case 'J':
            return 8;
        default:
            fail(DecodeErrorCode::UnsupportedFeature,
                    "Unsupported Java serialization primitive field type");
    }
}

} // namespace

JavaObjectStreamReader::JavaObjectStreamReader(std::vector<uint8_t> bytes) : mBytes(std::move(bytes)) {
    if (readRawUnsignedShort() != STREAM_MAGIC || readRawUnsignedShort() != STREAM_VERSION) {
        fail(DecodeErrorCode::InvalidHeader,
                "Invalid Java Object Serialization stream header");
    }
}

JavaObjectStreamReader::JavaObjectStreamReader(std::istream& input,
        std::uint64_t byteCount)
        : mInput(&input) {
    if (byteCount > std::numeric_limits<size_t>::max()) {
        fail(DecodeErrorCode::LimitExceeded,
                "Java serialization stream exceeds the addressable size");
    }
    mStreamRemaining = static_cast<size_t>(byteCount);
    if (readRawUnsignedShort() != STREAM_MAGIC || readRawUnsignedShort() != STREAM_VERSION) {
        fail(DecodeErrorCode::InvalidHeader,
                "Invalid Java Object Serialization stream header");
    }
}

bool JavaObjectStreamReader::readBoolean() {
    return readPrimitiveByte() != 0;
}

int8_t JavaObjectStreamReader::readByte() {
    return static_cast<int8_t>(readPrimitiveByte());
}

int16_t JavaObjectStreamReader::readShort() {
    uint16_t value = static_cast<uint16_t>(readPrimitiveByte()) << 8;
    value |= readPrimitiveByte();
    return static_cast<int16_t>(value);
}

int32_t JavaObjectStreamReader::readInt() {
    uint32_t value = static_cast<uint32_t>(readPrimitiveByte()) << 24;
    value |= static_cast<uint32_t>(readPrimitiveByte()) << 16;
    value |= static_cast<uint32_t>(readPrimitiveByte()) << 8;
    value |= readPrimitiveByte();
    return static_cast<int32_t>(value);
}

int64_t JavaObjectStreamReader::readLong() {
    uint64_t value = static_cast<uint64_t>(readPrimitiveByte()) << 56;
    value |= static_cast<uint64_t>(readPrimitiveByte()) << 48;
    value |= static_cast<uint64_t>(readPrimitiveByte()) << 40;
    value |= static_cast<uint64_t>(readPrimitiveByte()) << 32;
    value |= static_cast<uint64_t>(readPrimitiveByte()) << 24;
    value |= static_cast<uint64_t>(readPrimitiveByte()) << 16;
    value |= static_cast<uint64_t>(readPrimitiveByte()) << 8;
    value |= readPrimitiveByte();
    return static_cast<int64_t>(value);
}

float JavaObjectStreamReader::readFloat() {
    uint32_t bits = static_cast<uint32_t>(readInt());
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double JavaObjectStreamReader::readDouble() {
    uint64_t bits = static_cast<uint64_t>(readLong());
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

JavaObjectStreamReader::ObjectValue JavaObjectStreamReader::readObject() {
    drainPrimitiveBlock();
    return readContentObject();
}

bool JavaObjectStreamReader::hasRemainingBytes() {
    return mPrimitiveBlockRemaining > 0 || rawRemaining() > 0;
}

std::vector<uint8_t> JavaObjectStreamReader::remainingBytes() {
    drainPrimitiveBlock();
    if (mInput == nullptr && !mHasUnreadByte) {
        if (mPosition != 0) {
            mBytes.erase(mBytes.begin(),
                    mBytes.begin() + static_cast<std::ptrdiff_t>(mPosition));
        }
        mPosition = 0;
        return std::move(mBytes);
    }

    std::vector<uint8_t> result;
    try {
        result.resize(rawRemaining());
    } catch (const std::bad_alloc&) {
        fail(DecodeErrorCode::OutOfMemory,
                "Could not allocate remaining Java serialization bytes");
    } catch (const std::length_error&) {
        fail(DecodeErrorCode::LimitExceeded,
                "Remaining Java serialization bytes exceed the container limit");
    }
    if (!result.empty()) {
        readRawBytes(reinterpret_cast<char*>(result.data()), result.size());
    }
    return result;
}

void JavaObjectStreamReader::finishPrimitiveSection() {
    drainPrimitiveBlock();
}

uint8_t JavaObjectStreamReader::readRawByte() {
    if (mHasUnreadByte) {
        mHasUnreadByte = false;
        return mUnreadByte;
    }
    if (mInput != nullptr) {
        if (mStreamRemaining == 0) {
            fail(DecodeErrorCode::TruncatedInput,
                    "Java Object Serialization stream ended unexpectedly");
        }
        const int value = mInput->get();
        if (value == std::char_traits<char>::eof()) {
            fail(DecodeErrorCode::TruncatedInput,
                    "Java Object Serialization stream ended unexpectedly");
        }
        mStreamRemaining--;
        return static_cast<uint8_t>(value);
    }
    if (mPosition >= mBytes.size()) {
        fail(DecodeErrorCode::TruncatedInput,
                "Java Object Serialization stream ended unexpectedly");
    }
    return mBytes[mPosition++];
}

void JavaObjectStreamReader::unreadRawByte(uint8_t value) {
    if (mHasUnreadByte) {
        fail(DecodeErrorCode::InvalidValue,
                "Java serialization parser pushback overflow");
    }
    mHasUnreadByte = true;
    mUnreadByte = value;
}

uint8_t JavaObjectStreamReader::peekRawByte() {
    const uint8_t value = readRawByte();
    unreadRawByte(value);
    return value;
}

size_t JavaObjectStreamReader::rawRemaining() const {
    const size_t unreadCount = mHasUnreadByte ? 1 : 0;
    if (mInput != nullptr) {
        return mStreamRemaining + unreadCount;
    }
    return mBytes.size() - mPosition + unreadCount;
}

void JavaObjectStreamReader::readRawBytes(char* destination, size_t count) {
    if (count > rawRemaining()) {
        fail(DecodeErrorCode::TruncatedInput,
                "Java Object Serialization stream ended unexpectedly");
    }
    size_t copied = 0;
    if (mHasUnreadByte && count > 0) {
        destination[0] = static_cast<char>(readRawByte());
        copied = 1;
    }
    const size_t remainingCount = count - copied;
    if (remainingCount == 0) {
        return;
    }
    if (mInput != nullptr) {
        mInput->read(destination + static_cast<std::ptrdiff_t>(copied),
                static_cast<std::streamsize>(remainingCount));
        if (!*mInput) {
            fail(DecodeErrorCode::TruncatedInput,
                    "Java Object Serialization stream ended unexpectedly");
        }
        mStreamRemaining -= remainingCount;
        return;
    }
    std::copy_n(mBytes.data() + mPosition,
            remainingCount,
            destination + static_cast<std::ptrdiff_t>(copied));
    mPosition += remainingCount;
}

void JavaObjectStreamReader::skipRawBytes(size_t count) {
    if (count > rawRemaining()) {
        fail(DecodeErrorCode::TruncatedInput,
                "Java Object Serialization stream ended unexpectedly");
    }
    if (mHasUnreadByte && count > 0) {
        (void)readRawByte();
        count--;
    }
    if (count == 0) {
        return;
    }
    if (mInput != nullptr) {
        mInput->ignore(static_cast<std::streamsize>(count));
        if (static_cast<size_t>(mInput->gcount()) != count) {
            fail(DecodeErrorCode::TruncatedInput,
                    "Java Object Serialization stream ended unexpectedly");
        }
        mStreamRemaining -= count;
        return;
    }
    mPosition += count;
}

uint16_t JavaObjectStreamReader::readRawUnsignedShort() {
    uint16_t value = static_cast<uint16_t>(readRawByte()) << 8;
    value |= readRawByte();
    return value;
}

int16_t JavaObjectStreamReader::readRawShort() {
    return static_cast<int16_t>(readRawUnsignedShort());
}

int32_t JavaObjectStreamReader::readRawInt() {
    uint32_t value = static_cast<uint32_t>(readRawByte()) << 24;
    value |= static_cast<uint32_t>(readRawByte()) << 16;
    value |= static_cast<uint32_t>(readRawByte()) << 8;
    value |= readRawByte();
    return static_cast<int32_t>(value);
}

int64_t JavaObjectStreamReader::readRawLong() {
    uint64_t value = static_cast<uint64_t>(readRawByte()) << 56;
    value |= static_cast<uint64_t>(readRawByte()) << 48;
    value |= static_cast<uint64_t>(readRawByte()) << 40;
    value |= static_cast<uint64_t>(readRawByte()) << 32;
    value |= static_cast<uint64_t>(readRawByte()) << 24;
    value |= static_cast<uint64_t>(readRawByte()) << 16;
    value |= static_cast<uint64_t>(readRawByte()) << 8;
    value |= readRawByte();
    return static_cast<int64_t>(value);
}

std::string JavaObjectStreamReader::readRawUtf() {
    const size_t size = readRawUnsignedShort();
    if (size > MAX_STRING_BYTES) {
        fail(DecodeErrorCode::LimitExceeded,
                "Java serialization UTF value exceeds the string safety limit");
    }
    if (size > rawRemaining()) {
        fail(DecodeErrorCode::TruncatedInput,
                "Java serialization stream ended before the declared UTF value");
    }
    std::string value;
    try {
        value.resize(size);
    } catch (const std::bad_alloc&) {
        fail(DecodeErrorCode::OutOfMemory,
                "Could not allocate Java serialization UTF value");
    }
    if (!value.empty()) {
        readRawBytes(value.data(), value.size());
    }
    return value;
}

std::string JavaObjectStreamReader::readRawLongUtf() {
    const int64_t wireSize = readRawLong();
    if (wireSize < 0) {
        fail(DecodeErrorCode::InvalidLength,
                "Negative Java serialization long UTF length");
    }
    const uint64_t size = static_cast<uint64_t>(wireSize);
    if (size > MAX_STRING_BYTES) {
        fail(DecodeErrorCode::LimitExceeded,
                "Java serialization long UTF value exceeds the string safety limit");
    }
    if (size > static_cast<uint64_t>(rawRemaining())) {
        fail(DecodeErrorCode::TruncatedInput,
                "Java serialization stream ended before the declared long UTF value");
    }
    std::string value;
    try {
        value.resize(static_cast<size_t>(size));
    } catch (const std::bad_alloc&) {
        fail(DecodeErrorCode::OutOfMemory,
                "Could not allocate Java serialization long UTF value");
    }
    if (!value.empty()) {
        readRawBytes(value.data(), value.size());
    }
    return value;
}

uint8_t JavaObjectStreamReader::readPrimitiveByte() {
    ensurePrimitiveBytes(1);
    mPrimitiveBlockRemaining--;
    return readRawByte();
}

void JavaObjectStreamReader::ensurePrimitiveBytes(size_t count) {
    while (mPrimitiveBlockRemaining == 0) {
        uint8_t tag = readRawByte();
        if (tag == TC_RESET) {
            mHandles.clear();
            continue;
        }
        if (tag == TC_BLOCKDATA) {
            mPrimitiveBlockRemaining = readRawByte();
        } else if (tag == TC_BLOCKDATALONG) {
            int32_t size = readRawInt();
            if (size < 0) {
                fail(DecodeErrorCode::InvalidLength,
                        "Negative Java serialization block-data length");
            }
            mPrimitiveBlockRemaining = static_cast<size_t>(size);
        } else {
            fail(DecodeErrorCode::InvalidHeader,
                    "Expected Java serialization primitive block-data");
        }
        if (mPrimitiveBlockRemaining > MAX_BLOCK_DATA_BYTES) {
            fail(DecodeErrorCode::LimitExceeded,
                    "Java serialization block-data exceeds the safety limit");
        }
        if (mPrimitiveBlockRemaining > rawRemaining()) {
            fail(DecodeErrorCode::TruncatedInput,
                    "Java serialization stream ended before the declared block-data");
        }
    }
    if (mPrimitiveBlockRemaining < count) {
        fail(DecodeErrorCode::InvalidLength,
                "Primitive read crosses unsupported Java serialization block boundary");
    }
}

void JavaObjectStreamReader::drainPrimitiveBlock() {
    if (mPrimitiveBlockRemaining > 0) {
        if (mPrimitiveBlockRemaining > rawRemaining()) {
            fail(DecodeErrorCode::TruncatedInput,
                    "Java serialization stream ended before the declared block-data");
        }
        skipRawBytes(mPrimitiveBlockRemaining);
        mPrimitiveBlockRemaining = 0;
    }
}

bool JavaObjectStreamReader::nextTagIs(uint8_t tag) {
    return rawRemaining() > 0 && peekRawByte() == tag;
}

JavaObjectStreamReader::ObjectValue JavaObjectStreamReader::readContentObject() {
    NestingGuard nesting(mNestingDepth);
    uint8_t tag = readRawByte();
    switch (tag) {
        case TC_NULL:
            return {};
        case TC_REFERENCE:
            return readReference();
        case TC_STRING:
        case TC_LONGSTRING:
            return readNewString(tag);
        case TC_CLASS:
            return readNewClass();
        case TC_CLASSDESC:
        case TC_PROXYCLASSDESC:
            unreadRawByte(tag);
            return {ObjectKind::ClassDesc, assignHandle(readClassDesc()), "", ""};
        case TC_OBJECT:
            return readNewObject();
        case TC_ARRAY:
            return readNewArray();
        case TC_RESET:
            mHandles.clear();
            return readContentObject();
        default:
            fail(DecodeErrorCode::UnsupportedFeature,
                    "Unsupported Java serialization object tag");
    }
}

JavaObjectStreamReader::ObjectValue JavaObjectStreamReader::readNewString(uint8_t tag) {
    int32_t handle = assignHandle(ObjectKind::String);
    std::string value = tag == TC_STRING ? readRawUtf() : readRawLongUtf();
    mHandles[static_cast<size_t>(handle - BASE_WIRE_HANDLE)].stringValue = value;
    return {ObjectKind::String, handle, "java.lang.String", value};
}

JavaObjectStreamReader::ObjectValue JavaObjectStreamReader::readNewClass() {
    std::shared_ptr<ClassDesc> classDesc = readClassDesc();
    int32_t handle = assignHandle(ObjectKind::Class);
    return {ObjectKind::Class, handle, classDesc ? classDesc->name : "", ""};
}

JavaObjectStreamReader::ObjectValue JavaObjectStreamReader::readNewObject() {
    std::shared_ptr<ClassDesc> classDesc = readClassDesc();
    int32_t handle = assignHandle(ObjectKind::Object);
    mHandles[static_cast<size_t>(handle - BASE_WIRE_HANDLE)].classDesc = classDesc;
    skipClassData(classDesc);
    return {ObjectKind::Object, handle, classDesc ? classDesc->name : "", ""};
}

JavaObjectStreamReader::ObjectValue JavaObjectStreamReader::readNewArray() {
    std::shared_ptr<ClassDesc> classDesc = readClassDesc();
    int32_t handle = assignHandle(ObjectKind::Array);
    mHandles[static_cast<size_t>(handle - BASE_WIRE_HANDLE)].classDesc = classDesc;
    int32_t size = readRawInt();
    if (size < 0) {
        fail(DecodeErrorCode::InvalidLength,
                "Negative Java serialization array length");
    }
    if (static_cast<size_t>(size) > MAX_ARRAY_ENTRIES) {
        fail(DecodeErrorCode::LimitExceeded,
                "Java serialization array exceeds the entry safety limit");
    }
    skipArrayValues(classDesc ? classDesc->name : "", size);
    return {ObjectKind::Array, handle, classDesc ? classDesc->name : "", ""};
}

JavaObjectStreamReader::ObjectValue JavaObjectStreamReader::readReference() {
    int32_t wireHandle = readRawInt();
    Handle& handle = handleAt(wireHandle);
    return {ObjectKind::Reference, wireHandle, handle.classDesc ? handle.classDesc->name : "", handle.stringValue};
}

std::shared_ptr<JavaObjectStreamReader::ClassDesc> JavaObjectStreamReader::readClassDesc() {
    NestingGuard nesting(mNestingDepth);
    uint8_t tag = readRawByte();
    if (tag == TC_NULL) {
        return nullptr;
    }
    if (tag == TC_REFERENCE) {
        Handle& handle = handleAt(readRawInt());
        if (!handle.classDesc) {
            fail(DecodeErrorCode::InvalidValue,
                    "Java serialization reference is not a class descriptor");
        }
        return handle.classDesc;
    }
    if (tag == TC_PROXYCLASSDESC) {
        fail(DecodeErrorCode::UnsupportedFeature,
                "Java serialization proxy class descriptors are not supported");
    }
    if (tag != TC_CLASSDESC) {
        fail(DecodeErrorCode::InvalidHeader,
                "Expected Java serialization class descriptor");
    }
    return readNewClassDesc();
}

std::shared_ptr<JavaObjectStreamReader::ClassDesc> JavaObjectStreamReader::readNewClassDesc() {
    auto classDesc = std::make_shared<ClassDesc>();
    classDesc->name = readRawUtf();
    classDesc->serialVersionUid = readRawLong();
    assignHandle(classDesc);
    classDesc->flags = readRawByte();

    int16_t fieldCount = readRawShort();
    if (fieldCount < 0) {
        fail(DecodeErrorCode::InvalidCount,
                "Negative Java serialization field count");
    }
    if (static_cast<size_t>(fieldCount) > MAX_FIELD_COUNT) {
        fail(DecodeErrorCode::LimitExceeded,
                "Java serialization field count exceeds the safety limit");
    }
    classDesc->fields.reserve(static_cast<size_t>(fieldCount));
    for (int i = 0; i < fieldCount; i++) {
        FieldDesc field;
        field.typeCode = static_cast<char>(readRawByte());
        if (!isPrimitiveType(field.typeCode) && field.typeCode != 'L' && field.typeCode != '[') {
            fail(DecodeErrorCode::UnsupportedFeature,
                    "Unsupported Java serialization field type");
        }
        field.name = readRawUtf();
        if (field.typeCode == 'L' || field.typeCode == '[') {
            ObjectValue typeName = readContentObject();
            if (typeName.kind != ObjectKind::String && typeName.kind != ObjectKind::Reference) {
                fail(DecodeErrorCode::InvalidValue,
                        "Java serialization object field descriptor is not a string");
            }
            field.className = typeName.stringValue;
        }
        classDesc->fields.push_back(std::move(field));
    }

    readClassAnnotation();
    classDesc->superClass = readClassDesc();
    return classDesc;
}

void JavaObjectStreamReader::readClassAnnotation() {
    NestingGuard nesting(mNestingDepth);
    size_t entryCount = 0;
    size_t byteCount = 0;
    while (!nextTagIs(TC_ENDBLOCKDATA)) {
        if (++entryCount > MAX_ANNOTATION_ENTRIES) {
            fail(DecodeErrorCode::LimitExceeded,
                    "Java serialization annotation entry count exceeds the safety limit");
        }
        uint8_t tag = readRawByte();
        size_t blockSize = 0;
        if (tag == TC_BLOCKDATA) {
            blockSize = readRawByte();
        } else if (tag == TC_BLOCKDATALONG) {
            int32_t size = readRawInt();
            if (size < 0) {
                fail(DecodeErrorCode::InvalidLength,
                        "Negative Java serialization annotation block length");
            }
            blockSize = static_cast<size_t>(size);
        } else {
            unreadRawByte(tag);
            readContentObject();
            continue;
        }
        if (blockSize > MAX_ANNOTATION_BYTES - byteCount) {
            fail(DecodeErrorCode::LimitExceeded,
                    "Java serialization annotation bytes exceed the safety limit");
        }
        if (blockSize > rawRemaining()) {
            fail(DecodeErrorCode::TruncatedInput,
                    "Java serialization stream ended before annotation block-data");
        }
        byteCount += blockSize;
        skipRawBytes(blockSize);
    }
    readRawByte();
}

void JavaObjectStreamReader::skipClassData(const std::shared_ptr<ClassDesc>& classDesc) {
    NestingGuard nesting(mNestingDepth);
    if (!classDesc) {
        return;
    }
    skipClassData(classDesc->superClass);

    if ((classDesc->flags & SC_EXTERNALIZABLE) != 0) {
        if ((classDesc->flags & SC_BLOCK_DATA) == 0) {
            fail(DecodeErrorCode::UnsupportedFeature,
                    "Java serialization protocol-v1 Externalizable data is not supported");
        }
        readClassAnnotation();
        return;
    }
    if ((classDesc->flags & SC_SERIALIZABLE) == 0) {
        fail(DecodeErrorCode::UnsupportedFeature,
                "Unsupported Java serialization class flags");
    }

    for (const FieldDesc& field : classDesc->fields) {
        skipFieldValue(field);
    }
    if ((classDesc->flags & SC_WRITE_METHOD) != 0) {
        readClassAnnotation();
    }
}

void JavaObjectStreamReader::skipFieldValue(const FieldDesc& field) {
    if (field.typeCode == 'L' || field.typeCode == '[') {
        readContentObject();
        return;
    }
    size_t size = primitiveTypeSize(field.typeCode);
    if (size > rawRemaining()) {
        fail(DecodeErrorCode::TruncatedInput,
                "Java serialization field value ended unexpectedly");
    }
    skipRawBytes(size);
}

void JavaObjectStreamReader::skipArrayValues(const std::string& className, int32_t size) {
    if (className.size() < 2 || className[0] != '[') {
        fail(DecodeErrorCode::InvalidValue,
                "Java serialization array has invalid class name");
    }
    if (size < 0) {
        fail(DecodeErrorCode::InvalidLength,
                "Negative Java serialization array length");
    }
    if (static_cast<size_t>(size) > MAX_ARRAY_ENTRIES) {
        fail(DecodeErrorCode::LimitExceeded,
                "Java serialization array exceeds the entry safety limit");
    }
    char componentType = className[1];
    if (componentType == 'L' || componentType == '[') {
        for (int32_t i = 0; i < size; i++) {
            readContentObject();
        }
        return;
    }
    const size_t elementSize = primitiveTypeSize(componentType);
    const size_t entryCount = static_cast<size_t>(size);
    if (entryCount > rawRemaining() / elementSize) {
        fail(DecodeErrorCode::TruncatedInput,
                "Java serialization array values ended unexpectedly");
    }
    const size_t bytesToSkip = elementSize * entryCount;
    skipRawBytes(bytesToSkip);
}

int32_t JavaObjectStreamReader::assignHandle(ObjectKind kind) {
    if (mHandles.size() >= MAX_HANDLE_COUNT) {
        fail(DecodeErrorCode::LimitExceeded,
                "Java serialization handle count exceeds the safety limit");
    }
    Handle handle;
    handle.kind = kind;
    mHandles.push_back(std::move(handle));
    return BASE_WIRE_HANDLE + static_cast<int32_t>(mHandles.size() - 1);
}

int32_t JavaObjectStreamReader::assignHandle(std::shared_ptr<ClassDesc> classDesc) {
    if (mHandles.size() >= MAX_HANDLE_COUNT) {
        fail(DecodeErrorCode::LimitExceeded,
                "Java serialization handle count exceeds the safety limit");
    }
    Handle handle;
    handle.kind = ObjectKind::ClassDesc;
    handle.classDesc = std::move(classDesc);
    mHandles.push_back(std::move(handle));
    return BASE_WIRE_HANDLE + static_cast<int32_t>(mHandles.size() - 1);
}

JavaObjectStreamReader::Handle& JavaObjectStreamReader::handleAt(int32_t wireHandle) {
    int32_t index = wireHandle - BASE_WIRE_HANDLE;
    if (index < 0 || static_cast<size_t>(index) >= mHandles.size()) {
        fail(DecodeErrorCode::InvalidValue,
                "Invalid Java serialization reference handle");
    }
    return mHandles[static_cast<size_t>(index)];
}
