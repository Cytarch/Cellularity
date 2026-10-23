#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>
#include <vector>

class JavaObjectStreamReader {
public:
    static constexpr size_t MAX_HANDLE_COUNT = 4096;
    static constexpr size_t MAX_NESTING_DEPTH = 64;
    static constexpr size_t MAX_STRING_BYTES = 1024 * 1024;
    static constexpr size_t MAX_ARRAY_ENTRIES = 4096;
    static constexpr size_t MAX_FIELD_COUNT = 4096;
    static constexpr size_t MAX_BLOCK_DATA_BYTES = 1024 * 1024;
    static constexpr size_t MAX_ANNOTATION_ENTRIES = 4096;
    static constexpr size_t MAX_ANNOTATION_BYTES = 1024 * 1024;

    enum class ObjectKind {
        Null,
        Reference,
        String,
        Class,
        ClassDesc,
        Object,
        Array
    };

    struct ObjectValue {
        ObjectKind kind = ObjectKind::Null;
        int32_t handle = -1;
        std::string className;
        std::string stringValue;
    };

    explicit JavaObjectStreamReader(std::vector<uint8_t> bytes);
    JavaObjectStreamReader(std::istream& input, std::uint64_t byteCount);

    bool readBoolean();
    int8_t readByte();
    int16_t readShort();
    int32_t readInt();
    int64_t readLong();
    float readFloat();
    double readDouble();

    ObjectValue readObject();
    bool hasRemainingBytes();
    std::vector<uint8_t> remainingBytes();
    void finishPrimitiveSection();

private:
    struct FieldDesc {
        char typeCode = 0;
        std::string name;
        std::string className;
    };

    struct ClassDesc {
        std::string name;
        int64_t serialVersionUid = 0;
        uint8_t flags = 0;
        std::vector<FieldDesc> fields;
        std::shared_ptr<ClassDesc> superClass;
    };

    struct Handle {
        ObjectKind kind = ObjectKind::Null;
        std::shared_ptr<ClassDesc> classDesc;
        std::string stringValue;
    };

    uint8_t readRawByte();
    void unreadRawByte(uint8_t value);
    uint8_t peekRawByte();
    size_t rawRemaining() const;
    void readRawBytes(char* destination, size_t count);
    void skipRawBytes(size_t count);
    uint16_t readRawUnsignedShort();
    int16_t readRawShort();
    int32_t readRawInt();
    int64_t readRawLong();
    std::string readRawUtf();
    std::string readRawLongUtf();

    uint8_t readPrimitiveByte();
    void ensurePrimitiveBytes(size_t count);
    void drainPrimitiveBlock();
    bool nextTagIs(uint8_t tag);

    ObjectValue readContentObject();
    ObjectValue readNewString(uint8_t tag);
    ObjectValue readNewClass();
    ObjectValue readNewObject();
    ObjectValue readNewArray();
    ObjectValue readReference();
    std::shared_ptr<ClassDesc> readClassDesc();
    std::shared_ptr<ClassDesc> readNewClassDesc();
    void readClassAnnotation();
    void skipClassData(const std::shared_ptr<ClassDesc>& classDesc);
    void skipFieldValue(const FieldDesc& field);
    void skipArrayValues(const std::string& className, int32_t size);

    int32_t assignHandle(ObjectKind kind);
    int32_t assignHandle(std::shared_ptr<ClassDesc> classDesc);
    Handle& handleAt(int32_t wireHandle);

    std::vector<uint8_t> mBytes;
    std::istream* mInput = nullptr;
    size_t mPosition = 0;
    size_t mStreamRemaining = 0;
    bool mHasUnreadByte = false;
    uint8_t mUnreadByte = 0;
    size_t mPrimitiveBlockRemaining = 0;
    size_t mNestingDepth = 0;
    std::vector<Handle> mHandles;
};
