#include "vm.hpp"

#include "operators.hpp"

#include <cstddef>
#include <format>
#include <ostream>
#include <print>
#include <utility>

namespace sc {

namespace {

/// 有源码位置就用位置；没有（例如手写字节码）则退化成指令下标。
std::unexpected<Error> at(std::string message, std::size_t ip, int pos) {
    if (pos < 0) message += std::format(" (指令 {})", ip);
    return std::unexpected(Error{std::move(message), pos, false});
}

bool inRange(int index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}

} // namespace

Value VM::variable(int slot) const {
    if (!inRange(slot, variables_.size())) return Value();
    return variables_[static_cast<std::size_t>(slot)];
}

Status VM::run(const CompilationUnit& unit, const Config& config, std::ostream& out) {
    if (unit.numVars < 0) return fail("非法的变量槽数量");
    if (static_cast<int>(variables_.size()) < unit.numVars) {
        variables_.resize(static_cast<std::size_t>(unit.numVars));
    }
    stack_.clear();

    const ValueLimits limits = config.valueLimits();
    const std::size_t maxStack = static_cast<std::size_t>(config.maxStack);

    std::size_t pc = 0;
    while (pc < unit.code.size()) {
        const Instruction& inst = unit.code[pc];
        const std::size_t ip = pc++;

        switch (inst.op) {
            case OpCode::PushConst:
                if (!inRange(inst.operand, unit.constants.size()))
                    return at(std::format("非法常量索引 {}", inst.operand), ip, inst.pos);
                if (stack_.size() >= maxStack) return at("操作数栈溢出", ip, inst.pos);
                stack_.push_back(unit.constants[static_cast<std::size_t>(inst.operand)]);
                break;

            case OpCode::Load:
                if (!inRange(inst.operand, variables_.size()))
                    return at(std::format("非法变量索引 {}", inst.operand), ip, inst.pos);
                if (stack_.size() >= maxStack) return at("操作数栈溢出", ip, inst.pos);
                stack_.push_back(variables_[static_cast<std::size_t>(inst.operand)]);
                break;

            case OpCode::Store: {
                if (stack_.empty()) return at("栈下溢", ip, inst.pos);
                if (!inRange(inst.operand, variables_.size()))
                    return at(std::format("非法变量索引 {}", inst.operand), ip, inst.pos);
                variables_[static_cast<std::size_t>(inst.operand)] = std::move(stack_.back());
                stack_.pop_back();
                break;
            }

            case OpCode::Print:
                if (stack_.empty()) return at("栈下溢", ip, inst.pos);
                std::println(out, "{}", stack_.back().to_string(config.outputDigits));
                stack_.pop_back();
                break;

            case OpCode::Halt: return {};

            case OpCode::Count: return at("非法指令", ip, inst.pos);

            default: {
                // 运算符表驱动：这里不需要知道具体是哪个运算符。
                if (const BinaryOpInfo* info = binaryByOpcode(inst.op)) {
                    if (stack_.size() < 2) return at("栈下溢", ip, inst.pos);
                    Value rhs = std::move(stack_.back());
                    stack_.pop_back();
                    Value lhs = std::move(stack_.back());
                    stack_.pop_back();

                    Result<Value> result = info->eval(lhs, rhs, limits);
                    if (!result) return at(result.error().message, ip, inst.pos);
                    stack_.push_back(std::move(*result));
                    break;
                }
                if (const UnaryOpInfo* info = unaryByOpcode(inst.op)) {
                    if (stack_.empty()) return at("栈下溢", ip, inst.pos);
                    Value operand = std::move(stack_.back());
                    stack_.pop_back();

                    Result<Value> result = info->eval(operand, limits);
                    if (!result) return at(result.error().message, ip, inst.pos);
                    stack_.push_back(std::move(*result));
                    break;
                }
                // 到不了这里：opcodeCoverageOk() 已保证每条指令都有归属。
                return at(std::format("未实现的指令 {}", opName(inst.op)), ip, inst.pos);
            }
        }
    }
    return {};
}

} // namespace sc
