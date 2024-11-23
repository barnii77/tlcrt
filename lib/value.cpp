#include <stdexcept>
#include "tlc/rt.h"

namespace tlc {
namespace rt {
static bool compatibleTypes(ValueType a, ValueType b) {
    return a == ValueType::integer && b == ValueType::integer;
}

static void assertCompatibleTypes(ValueType a, ValueType b) {
    if (!compatibleTypes(a, b))
        throw std::runtime_error("incompatible types of operation operands");
}

Value Value::toInteger() const {
    return Value(data, ValueType::integer);
}

#define DEFINE_BINARY_OPERATOR(sign) \
Value Value::operator sign(const Value& other) const { \
    assertCompatibleTypes(type, other.type); \
    return Value(data sign other.data, ValueType::integer); \
}

DEFINE_BINARY_OPERATOR(+);
DEFINE_BINARY_OPERATOR(-);
DEFINE_BINARY_OPERATOR(*);
DEFINE_BINARY_OPERATOR(/);
DEFINE_BINARY_OPERATOR(%);
DEFINE_BINARY_OPERATOR(&);
DEFINE_BINARY_OPERATOR(|);
DEFINE_BINARY_OPERATOR(&&);
DEFINE_BINARY_OPERATOR(||);
DEFINE_BINARY_OPERATOR(<);
DEFINE_BINARY_OPERATOR(>);
DEFINE_BINARY_OPERATOR(<=);
DEFINE_BINARY_OPERATOR(>=);
DEFINE_BINARY_OPERATOR(==);
DEFINE_BINARY_OPERATOR(!=);
DEFINE_BINARY_OPERATOR(^);

Value Value::operator!() const {
    if (type != ValueType::integer)
        throw std::runtime_error("cannot apply ! operator on memory handle");
    return Value(!data, type);
}

Value Value::operator~() const {
    if (type != ValueType::integer)
        throw std::runtime_error("cannot apply ~ operator on memory handle");
    return Value(~data, type);
}
} // namespace rt
} // namespace tlc
