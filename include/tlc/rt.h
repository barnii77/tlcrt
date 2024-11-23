#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ast.h"

namespace tlc {
namespace rt {
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using VarT = i64;
using FunT = i64;

enum class ValueType : std::uint8_t {
    integer,
    memory_handle,
};

struct Value {
    i64 data;
    ValueType type;

    Value(i64 data, ValueType type);
    Value() = default;

    Value toInteger() const;
    Value operator+(const Value& other) const;
    Value operator-(const Value& other) const;
    Value operator*(const Value& other) const;
    Value operator/(const Value& other) const;
    Value operator%(const Value& other) const;
    Value operator&(const Value& other) const;
    Value operator|(const Value& other) const;
    Value operator&&(const Value& other) const;
    Value operator||(const Value& other) const;
    Value operator!() const;
    Value operator~() const;
    Value operator<(const Value& other) const;
    Value operator>(const Value& other) const;
    Value operator<=(const Value& other) const;
    Value operator>=(const Value& other) const;
    Value operator==(const Value& other) const;
    Value operator!=(const Value& other) const;
    Value operator^(const Value& other) const;
};

struct MemoryHandle {
    std::vector<Value> data;
    i64 alloc_id;
    i32 ref_count;
    // List of all flags:
    // -) flags & 1 -> marked reachable by major GC
    i32 flags{0};

    MemoryHandle(std::vector<Value> data, i64 alloc_id, i32 ref_count);
};

// WARNING: not thread safe
class Context {
    i64 m_alloc_counter{1};
    std::unordered_map<VarT, Value> m_data;
    std::unordered_map<FunT, FunctionExecutable> m_functions;
    std::unordered_map<i64, MemoryHandle> m_mem_handles;
    std::vector<i64> m_gc_candidates;

    // reuse heap allocated variables of majorGC
    std::unordered_set<i64> m_magc_visited_mem_handles;
    std::unordered_set<i64> m_magc_new_mem_handles;
    std::unordered_set<i64> m_magc_next_mem_handles;
    std::vector<i64> m_tmp_garbage_allocs;
    
    void incref(const Value& data);
    void decref(const Value& data);
    void assertValidMemHandle(const Value& data);
    void decoupleMemHandle(const MemoryHandle& mh);
    void destroyMemHandle(const MemoryHandle& mh);
    void releaseGarbage(const std::vector<i64>& garbage_allocs);

public:
    Context() = default;

    void defineFunction(FunT id, FunctionExecutable fun);
    void eraseFunction(FunT id);
    void assign(VarT id, Value value);
    void erase(VarT id);
    bool varIsDefined(VarT id);
    bool funIsDefined(FunT id);

    Value alloc(i64 size);
    void push(Value array, Value value);
    Value pop(Value array);
    void write(Value array, i64 index, Value value);
    Value read(Value array, i64 index);

    void minorGC();
    void majorGC();
};
} // namespace rt
} // namespace tlc