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
#ifndef NO_MINOR_GC
    if (value.type == ValueType::memory_handle)
        incref(value);
#endif
    mh.data.emplace_back(value);
}

Value Context::pop(Value array) {
    assertValidMemHandle(array);
    MemoryHandle& mh = m_mem_handles.at(array.data);
    if (mh.data.size() == 0)
        throw std::runtime_error("cannot pop from empty array");
    Value value = mh.data.back();
#ifndef NO_MINOR_GC
    if (value.type == ValueType::memory_handle)
        decref(value);
#endif
    mh.data.pop_back();
    return value;
}

void Context::write(Value array, i64 index, Value value) {
    assertValidMemHandle(array);
    MemoryHandle& mh = m_mem_handles.at(array.data);
    if (index < 0 || index >= mh.data.size())
        throw std::runtime_error("invalid index for data chunk of size " + std::to_string(mh.data.size()));
#ifndef NO_MINOR_GC
    Value& current = mh.data[index];
    if (current.type == ValueType::memory_handle)
        decref(current);
    if (value.type == ValueType::memory_handle)
        incref(value);
#endif
    mh.data[index] = value;
}

Value Context::read(Value array, i64 index) {
    assertValidMemHandle(array);
    MemoryHandle& mh = m_mem_handles.at(array.data);
    if (index < 0 || index >= mh.data.size())
        throw std::runtime_error("invalid index for data chunk of size " + std::to_string(mh.data.size()));
    return mh.data[index];
}

void Context::defineFunction(FunT id, void *fun) {
    m_functions[id] = fun;
}

void Context::eraseFunction(FunT id) {
    if (!funIsDefined(id))
        throw std::runtime_error("tried to erase undefined function");
    m_functions.erase(id);
}

void Context::assign(VarT id, Value value) {
#ifndef NO_MINOR_GC
    Value& current = m_data[id];
    if (current.type == ValueType::memory_handle)
        decref(current);
    if (value.type == ValueType::memory_handle)
        incref(value);
#endif
    m_data[id] = value;
}

void Context::erase(VarT id) {
    if (!varIsDefined(id))
        throw std::runtime_error("tried to erase undefined variable");
#ifndef NO_MINOR_GC
    const Value& value = m_data[id];
    if (value.type == ValueType::memory_handle)
        decref(value);
#endif
    m_data.erase(id);
}

bool Context::varIsDefined(VarT id) {
    return m_data.find(id) != m_data.end();
}

bool Context::funIsDefined(FunT id) {
    return m_functions.find(id) != m_functions.end();
}

/// decref all live peers
void Context::decoupleMemHandle(const MemoryHandle& mh) {
#ifndef NO_MINOR_GC
    for (const Value& v : mh.data)
        if (v.type == ValueType::memory_handle && m_mem_handles.find(v.data) != m_mem_handles.end())
            decref(v);
#endif
}

/// free memory
void Context::destroyMemHandle(const MemoryHandle& mh) {
    m_mem_handles.erase(mh.alloc_id);
}

/// batch release garbage memory handles by first decoupling all of them, then destroying them
void Context::releaseGarbage(const std::vector<i64>& garbage_allocs) {
    m_release_tmp_valid_garbage_allocs.clear();
    for (const auto ga : garbage_allocs)
        if (m_mem_handles.find(ga) != m_mem_handles.end())
            m_release_tmp_valid_garbage_allocs.emplace_back(ga);
    for (const auto ga : m_release_tmp_valid_garbage_allocs) {
        const MemoryHandle& mh = m_mem_handles.at(ga);
        decoupleMemHandle(mh);
    }
    for (const auto ga : m_release_tmp_valid_garbage_allocs) {
        const MemoryHandle& mh = m_mem_handles.at(ga);
        destroyMemHandle(mh);
    }
}

/// ref counting without cycle detection (thus major GC is needed)
void Context::minorGC() {
#ifndef NO_MINOR_GC
    m_migc_tmp_garbage_allocs.clear();

    for (i64 p : m_gc_candidates) {
        if (m_mem_handles.find(p) == m_mem_handles.end())
            continue;  // already invalidated by majorGC
        const MemoryHandle& mh = m_mem_handles.at(p);
        if (mh.ref_count <= 0)
            m_migc_tmp_garbage_allocs.emplace_back(p);
    }
    releaseGarbage(m_migc_tmp_garbage_allocs);

    m_gc_candidates.clear();
#endif
}

/// global mark and sweep
void Context::majorGC(i64 max_steps) {
    if (max_steps == -1) {
        m_magc_tmp_garbage_allocs.clear();
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
                for (Value& v : mh.data) {
                    if (v.type == ValueType::memory_handle
                        && m_magc_visited_mem_handles.find(v.data) == m_magc_visited_mem_handles.end()) {
                        m_magc_new_mem_handles.emplace(v.data);
                    }
                }
            }

            std::unordered_set<i64> tmp = std::move(m_magc_next_mem_handles);
            m_magc_next_mem_handles = m_magc_new_mem_handles;
            m_magc_new_mem_handles = std::move(tmp);
            m_magc_new_mem_handles.clear();
        }

        for (const auto& it : m_mem_handles) {
            const MemoryHandle& mh = it.second;
            if (!(mh.flags & 0x1))
                m_magc_tmp_garbage_allocs.emplace_back(mh.alloc_id);
        }
        releaseGarbage(m_magc_tmp_garbage_allocs);
    } else {
        if (m_magc_state == 0) {
            m_magc_tmp_garbage_allocs.clear();
            m_magc_next_mem_handles.clear();
            m_magc_visited_mem_handles.clear();
            m_magc_new_mem_handles.clear();
            m_magc_state++;
        }

        if (m_magc_state == 1) {
            for (auto& it : m_mem_handles) {
                MemoryHandle& mh = it.second;
                mh.flags &= 0xFFFFFFFE;
            }
            m_magc_state++;
        }

        if (m_magc_state == 2) {
            for (const auto& it : m_data) {
                const Value& v = it.second;
                if (v.type == ValueType::memory_handle)
                    m_magc_next_mem_handles.emplace(v.data);
            }
            m_magc_state++;
        }

        i64 step_counter = 0;
        while (!m_magc_next_mem_handles.empty()) {
            if (m_magc_state == 3) {
                for (i64 p : m_magc_next_mem_handles)
                    m_magc_visited_mem_handles.emplace(p);
                m_magc_state++;
            }

            if (m_magc_state == 4) {
                i64 ih = 0, ihe = 0;
                for (i64 p : m_magc_next_mem_handles) {
                    if (ih < m_magc_last_handle) {
                        ih++;
                        continue;
                    }
                    MemoryHandle& mh = m_mem_handles.at(p);
                    mh.flags |= 0x1;
                    for (Value& v : mh.data) {
                        if (ihe < m_magc_last_handle_entry) {
                            ihe++;
                            continue;
                        }
                        if (step_counter >= max_steps)
                            return;
                        if (v.type == ValueType::memory_handle
                            && m_magc_visited_mem_handles.find(v.data) == m_magc_visited_mem_handles.end()) {
                            m_magc_new_mem_handles.emplace(v.data);
                        }
                        m_magc_last_handle_entry++, ihe++, step_counter++;
                    }
                    m_magc_last_handle_entry = 0, ihe = 0;
                    m_magc_last_handle++, ih++;
                }
                m_magc_state++;
            }

            if (m_magc_state == 5) {
                m_magc_last_handle = 0;
                m_magc_last_handle_entry = 0;
                std::unordered_set<i64> tmp = std::move(m_magc_next_mem_handles);
                m_magc_next_mem_handles = m_magc_new_mem_handles;
                m_magc_new_mem_handles = std::move(tmp);
                m_magc_new_mem_handles.clear();
                m_magc_state = 3;
            }
        }

        for (const auto& it : m_mem_handles) {
            const MemoryHandle& mh = it.second;
            if (!(mh.flags & 0x1))
                m_magc_tmp_garbage_allocs.emplace_back(mh.alloc_id);
        }
        releaseGarbage(m_magc_tmp_garbage_allocs);
        m_magc_state = 0;
    }
}
} // namespace rt
} // namespace tlc
