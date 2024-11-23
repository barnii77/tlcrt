#include <iterator>
#include <unordered_set>
#include <utility>
#include <unordered_map>
#include <stdexcept>
#include "tlc/rt.h"

namespace tlc {
namespace rt {
Value::Value(i64 data, ValueType type)
    : data(data), type(type) {}

MemoryHandle::MemoryHandle(std::vector<Value> data, i64 alloc_id, i32 ref_count)
    : data(std::move(data)), alloc_id(alloc_id), ref_count(ref_count), flags(0) {}

void Context::assertValidMemHandle(const Value& value) {
    if (value.type != ValueType::memory_handle || m_mem_handles.find(value.data) == m_mem_handles.end())
        throw std::runtime_error("invalid memory handle");
}

void Context::incref(const Value& mem_handle) {
    assertValidMemHandle(mem_handle);
    i32& ref_count = m_mem_handles.at(mem_handle.data).ref_count;
    ref_count++;
}

void Context::decref(const Value& mem_handle) {
    assertValidMemHandle(mem_handle);
    i32& ref_count = m_mem_handles.at(mem_handle.data).ref_count;
    if (--ref_count <= 0)
        m_gc_candidates.emplace_back(mem_handle.data);
}

Value Context::alloc(i64 size) {
    if (size < 0)
        throw std::runtime_error("size < 0 is not allowed in allocation");
    i64 alloc_id = m_alloc_counter++;
    m_mem_handles.emplace(alloc_id, std::move(MemoryHandle(std::vector<Value>(size), alloc_id, 0)));
    return Value(alloc_id, ValueType::memory_handle);
}

void Context::push(Value array, Value value) {
    assertValidMemHandle(array);
    MemoryHandle& mh = m_mem_handles.at(array.data);
    if (value.type == ValueType::memory_handle)
        incref(value);
    mh.data.emplace_back(value);
}

Value Context::pop(Value array) {
    assertValidMemHandle(array);
    MemoryHandle& mh = m_mem_handles.at(array.data);
    if (mh.data.size() == 0)
        throw std::runtime_error("cannot pop from empty array");
    Value value = mh.data.back();
    if (value.type == ValueType::memory_handle)
        decref(value);
    mh.data.pop_back();
    return value;
}

void Context::write(Value array, i64 index, Value value) {
    assertValidMemHandle(array);
    MemoryHandle& mh = m_mem_handles.at(array.data);
    if (index < 0 || index >= mh.data.size())
        throw std::runtime_error("invalid index for data chunk of size " + std::to_string(mh.data.size()));
    Value& current = mh.data[index];
    if (current.type == ValueType::memory_handle)
        decref(current);
    if (value.type == ValueType::memory_handle)
        incref(value);
    mh.data[index] = value;
}

Value Context::read(Value array, i64 index) {
    assertValidMemHandle(array);
    MemoryHandle& mh = m_mem_handles.at(array.data);
    if (index < 0 || index >= mh.data.size())
        throw std::runtime_error("invalid index for data chunk of size " + std::to_string(mh.data.size()));
    return mh.data[index];
}

void Context::defineFunction(FunT id, FunctionExecutable fun) {
    m_functions[id] = fun;
}

void Context::eraseFunction(FunT id) {
    if (!funIsDefined(id))
        throw std::runtime_error("tried to erase undefined function");
    m_functions.erase(id);
}

void Context::assign(VarT id, Value value) {
    Value& current = m_data[id];
    if (current.type == ValueType::memory_handle)
        decref(current);
    if (value.type == ValueType::memory_handle)
        incref(value);
    m_data[id] = value;
}

void Context::erase(VarT id) {
    if (!varIsDefined(id))
        throw std::runtime_error("tried to erase undefined variable");
    const Value& value = m_data[id];
    if (value.type == ValueType::memory_handle)
        decref(value);
    m_data.erase(id);
}

bool Context::varIsDefined(VarT id) {
    return m_data.find(id) != m_data.end();
}

bool Context::funIsDefined(FunT id) {
    return m_functions.find(id) != m_functions.end();
}

/// decref all peers
void Context::decoupleMemHandle(const MemoryHandle& mh) {
    for (const Value& v : mh.data)
        if (v.type == ValueType::memory_handle)
            decref(v);
}

/// free memory
void Context::destroyMemHandle(const MemoryHandle& mh) {
    m_mem_handles.erase(mh.alloc_id);
}

/// batch release garbage memory handles by first decoupling all of them, then destroying them
void Context::releaseGarbage(const std::vector<i64>& garbage_allocs) {
    for (const auto ga : garbage_allocs) {
        const MemoryHandle& mh = m_mem_handles.at(ga);
        if (!(mh.flags & 0x1))
            decoupleMemHandle(mh);
    }
    for (const auto ga : garbage_allocs) {
        const MemoryHandle& mh = m_mem_handles.at(ga);
        if (!(mh.flags & 0x1))
            destroyMemHandle(mh);
    }
}

/// ref counting without cycle detection (thus major GC is needed)
void Context::minorGC() {
    m_tmp_garbage_allocs.clear();

    for (i64 p : m_gc_candidates) {
        if (m_mem_handles.find(p) == m_mem_handles.end())
            continue;  // already invalidated by majorGC
        const MemoryHandle& mh = m_mem_handles.at(p);
        if (mh.ref_count <= 0)
            m_tmp_garbage_allocs.emplace_back(p);
    }
    releaseGarbage(m_tmp_garbage_allocs);

    m_gc_candidates.clear();
}

/// global mark and sweep
void Context::majorGC() {
    m_tmp_garbage_allocs.clear();
    m_magc_next_mem_handles.clear();
    m_magc_visited_mem_handles.clear();
    m_magc_new_mem_handles.clear();

    for (auto& it : m_mem_handles) {
        MemoryHandle& mh = it.second;
        mh.flags &= 0xFFFFFFFE;
    }

    for (const auto& it : m_data) {
        const Value& v = it.second;
        if (v.type == ValueType::memory_handle)
            m_magc_next_mem_handles.emplace(v.data);
    }

    while (!m_magc_next_mem_handles.empty()) {
        for (i64 p : m_magc_next_mem_handles)
            m_magc_visited_mem_handles.emplace(p);

        for (i64 p : m_magc_next_mem_handles) {
            MemoryHandle& mh = m_mem_handles.at(p);
            mh.flags |= 0x1;
            for (Value& v : mh.data)
                if (v.type == ValueType::memory_handle
                    && m_magc_visited_mem_handles.find(v.data) != m_magc_visited_mem_handles.end())
                    m_magc_new_mem_handles.emplace(v.data);
        }

        std::unordered_set<i64> tmp = std::move(m_magc_next_mem_handles);
        m_magc_next_mem_handles = m_magc_new_mem_handles;
        m_magc_new_mem_handles = std::move(tmp);
        m_magc_new_mem_handles.clear();
    }

    for (const auto& it : m_mem_handles) {
        const MemoryHandle& mh = it.second;
        if (!(mh.flags & 0x1))
            m_tmp_garbage_allocs.emplace_back(mh.alloc_id);
    }
    releaseGarbage(m_tmp_garbage_allocs);
}
} // namespace rt
} // namespace tlc
