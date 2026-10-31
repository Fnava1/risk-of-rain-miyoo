import os
import re

def build_vm_code():
    vm_path = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "vm.c")
    with open(vm_path, "r", encoding="utf-8", newline="") as f:
        content = f.read().replace("\r\n", "\n")

    unwind_anchor = """// Unwinds and frees the VMStack down to the newStackTop
static void unwindVMStack(VMContext* ctx, int32_t newStackTop) {
    repeat(ctx->stack.top - newStackTop, i) {
        RValue_free(&ctx->stack.slots[i + newStackTop]);
    }
    ctx->stack.top = newStackTop;
}"""

    macros_and_debug = """// Unwinds and frees the VMStack down to the newStackTop
static void unwindVMStack(VMContext* ctx, int32_t newStackTop) {
    repeat(ctx->stack.top - newStackTop, i) {
        RValue_free(&ctx->stack.slots[i + newStackTop]);
    }
    ctx->stack.top = newStackTop;
}

#ifndef VM_LIKELY
#if defined(__GNUC__) || defined(__clang__)
#define VM_LIKELY(x)   __builtin_expect(!!(x), 1)
#define VM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define VM_LIKELY(x)   (x)
#define VM_UNLIKELY(x) (x)
#endif
#endif

#if (defined(__GNUC__) || defined(__clang__)) && !defined(DISABLE_COMPUTED_GOTO)
#define VM_USE_COMPUTED_GOTO 1
#else
#define VM_USE_COMPUTED_GOTO 0
#endif

#if VM_USE_COMPUTED_GOTO
#define TARGET(op) do_##op
#define DISPATCH() VM_DISPATCH_STEP()
#else
#define TARGET(op) case op
#define DISPATCH() break
#endif

#if defined(ENABLE_VM_GML_PROFILER) || defined(ENABLE_VM_OPCODE_PROFILER) || defined(ENABLE_VM_TRACING)
static void NOINLINE vm_dispatch_debug_slow(VMContext* ctx, uint32_t instrAddr, uint32_t instr, const uint8_t* extraData, uint8_t opcode, RValue* slots) {
#ifdef ENABLE_VM_GML_PROFILER
    if (ctx->profiler != nullptr)
        Profiler_tickInstruction(ctx->profiler);
#endif
#ifdef ENABLE_VM_OPCODE_PROFILER
    if (ctx->opcodeProfilerEnabled) {
        ctx->opcodeCounts[opcode]++;
        ctx->opcodeVariantCounts[opcode * 256 + instrType1(instr) * 16 + instrType2(instr)]++;
        if (opcode == OP_BREAK) {
            int16_t breakType = instrInstanceType(instr);
            int idx = -breakType;
            if (idx >= 0 && 64 > idx) {
                ctx->breakSubOpCounts[idx]++;
            }
        }
        uint8_t rvTypeA = 0xFF, rvTypeB = 0xF;
        switch (opcode) {
            case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD:
            case OP_ADD: case OP_SUB: case OP_AND: case OP_OR:
            case OP_XOR: case OP_SHL: case OP_SHR: case OP_CMP:
                if (ctx->stack.top >= 2) {
                    rvTypeA = slots[ctx->stack.top - 2].type;
                    rvTypeB = slots[ctx->stack.top - 1].type;
                }
                break;
            case OP_NEG: case OP_NOT: case OP_CONV:
                if (ctx->stack.top >= 1) {
                    rvTypeA = slots[ctx->stack.top - 1].type;
                }
                break;
        }
        if (rvTypeA != 0xFF) {
            ctx->opcodeRValueTypeCounts[opcode * 256 + (rvTypeA & 0xF) * 16 + (rvTypeB & 0xF)]++;
        }
    }
#endif
#ifdef ENABLE_VM_TRACING
    if (shlen(ctx->opcodesToBeTraced) > 0 && ctx->runner->frameCount >= ctx->traceBytecodeAfterFrame) {
        if (shgeti(ctx->opcodesToBeTraced, "*") != -1 || shgeti(ctx->opcodesToBeTraced, ctx->currentCodeName) != -1) {
            char opcodeStr[32], operandStr[256] = "", commentStr[128] = "";
            formatInstruction(ctx, ctx->bytecodeBase, instrAddr, instr, extraData, opcodeStr, sizeof(opcodeStr), operandStr, sizeof(operandStr), commentStr, sizeof(commentStr));

            char* stackBuf = formatStackContents(ctx);

            if (operandStr[0] != '\\0') {
                logInfo("VM: [%s] @%04X (%d) [0x%08X] %s %s [stack=%d] %s\\n", ctx->currentCodeName, instrAddr, instrAddr, instr, opcodeStr, operandStr, ctx->stack.top, stackBuf);
            } else {
                logInfo("VM: [%s] @%04X (%d) [0x%08X] %s [stack=%d] %s\\n", ctx->currentCodeName, instrAddr, instrAddr, instr, opcodeStr, ctx->stack.top, stackBuf);
            }
            free(stackBuf);
        }
    }
#endif
}

static inline void vm_check_debug(VMContext* ctx, uint32_t instrAddr, uint32_t instr, const uint8_t* extraData, uint8_t opcode, RValue* slots) {
    if (VM_UNLIKELY(
#ifdef ENABLE_VM_GML_PROFILER
        (ctx->profiler != nullptr) ||
#endif
#ifdef ENABLE_VM_OPCODE_PROFILER
        ctx->opcodeProfilerEnabled ||
#endif
#ifdef ENABLE_VM_TRACING
        (shlen(ctx->opcodesToBeTraced) > 0 && ctx->runner->frameCount >= ctx->traceBytecodeAfterFrame) ||
#endif
        0)) {
        vm_dispatch_debug_slow(ctx, instrAddr, instr, extraData, opcode, slots);
    }
}
#else
static inline void vm_check_debug(VMContext* ctx, uint32_t instrAddr, uint32_t instr, const uint8_t* extraData, uint8_t opcode, RValue* slots) {
    (void)ctx; (void)instrAddr; (void)instr; (void)extraData; (void)opcode; (void)slots;
}
#endif

#if VM_USE_COMPUTED_GOTO
#ifdef ENABLE_WAD17
#define VM_CHECK_EXCEPTION() do { \\
    if (VM_UNLIKELY(ctx->exception != nullptr)) goto handle_exception; \\
} while (0)
#else
#define VM_CHECK_EXCEPTION() do {} while (0)
#endif

#define VM_DISPATCH_STEP() do { \\
    VM_CHECK_EXCEPTION(); \\
    if (VM_UNLIKELY(ip >= codeEnd)) goto loop_end; \\
    instrAddr = ip; \\
    instr = BinaryUtils_readUint32Aligned(bytecodeBase + ip); \\
    ip += 4; \\
    extraData = bytecodeBase + ip; \\
    if (instrHasExtraData(instr)) { \\
        ip += extraDataSize(instrType1(instr)); \\
    } \\
    opcode = instrOpcode(instr); \\
    vm_check_debug(ctx, instrAddr, instr, extraData, opcode, slots); \\
    goto *dispatch_table[opcode]; \\
} while (0)
#endif"""

    assert unwind_anchor in content, "unwind_anchor not found"
    content = content.replace(unwind_anchor, macros_and_debug, 1)

    # Now isolate executeLoop
    start_tag = "static RValue executeLoop(VMContext* ctx) {"
    end_tag = "// Rewrites WAD 14 bytecode opcodes to use WAD 16 bytecode opcodes"

    start_idx = content.find(start_tag)
    end_idx = content.find(end_tag)
    assert start_idx != -1 and end_idx != -1, "executeLoop boundaries not found"

    pre_loop = content[:start_idx]
    loop_body = content[start_idx:end_idx]
    post_loop = content[end_idx:]

    # Transform loop_body
    old_loop_head = """static RValue executeLoop(VMContext* ctx) {
    // codeEnd and bytecodeBase are invariant for the lifetime of this executeLoop call, so let's hoist them to avoid the compiler emitting code to
    // reload the values at the end of every iteration.
    const uint32_t codeEnd = ctx->codeEnd;
    const uint8_t* const bytecodeBase = ctx->bytecodeBase;
    // If you just joined the stream: ip is short for instruction pointer chat
    // The ip is mutable, so we need to use VM_SYNC_IP and VM_RELOAD_IP every time an opcode handler may access it or write to it
    uint32_t ip = ctx->ip;

    RValue* restrict slots = ctx->stack.slots;

    // Some opcodes have their handler or parts of their handler inlined
    // Those are opcodes that during real gameplay (using "--profile-opcodes") shown that, with inlining and keeping only the frequently called handle parts, we could squeeze MORE performance from the interpreter!
    while (codeEnd > ip) {"""

    new_loop_head = """static RValue executeLoop(VMContext* ctx) {
    // codeEnd and bytecodeBase are invariant for the lifetime of this executeLoop call, so let's hoist them to avoid the compiler emitting code to
    // reload the values at the end of every iteration.
    const uint32_t codeEnd = ctx->codeEnd;
    const uint8_t* const bytecodeBase = ctx->bytecodeBase;
    // If you just joined the stream: ip is short for instruction pointer chat
    // The ip is mutable, so we need to use VM_SYNC_IP and VM_RELOAD_IP every time an opcode handler may access it or write to it
    uint32_t ip = ctx->ip;

    RValue* restrict slots = ctx->stack.slots;
    uint32_t instrAddr = 0;
    uint32_t instr = 0;
    const uint8_t* extraData = nullptr;
    uint8_t opcode = 0;

#if VM_USE_COMPUTED_GOTO
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#endif
    static const void* const dispatch_table[256] = {
        [0 ... 255]   = &&do_unknown,
        [OP_CONV]     = &&do_OP_CONV,
        [OP_MUL]      = &&do_OP_MUL,
        [OP_DIV]      = &&do_OP_DIV,
        [OP_REM]      = &&do_OP_REM,
        [OP_MOD]      = &&do_OP_MOD,
        [OP_ADD]      = &&do_OP_ADD,
        [OP_SUB]      = &&do_OP_SUB,
        [OP_AND]      = &&do_OP_AND,
        [OP_OR]       = &&do_OP_OR,
        [OP_XOR]      = &&do_OP_XOR,
        [OP_NEG]      = &&do_OP_NEG,
        [OP_NOT]      = &&do_OP_NOT,
        [OP_SHL]      = &&do_OP_SHL,
        [OP_SHR]      = &&do_OP_SHR,
        [OP_CMP]      = &&do_OP_CMP,
        [OP_POP]      = &&do_OP_POP,
        [OP_PUSHI]    = &&do_OP_PUSHI,
        [OP_DUP]      = &&do_OP_DUP,
        [OP_CALLV]    = &&do_OP_CALLV,
        [OP_RET]      = &&do_OP_RET,
        [OP_EXIT]     = &&do_OP_EXIT,
        [OP_POPZ]     = &&do_OP_POPZ,
        [OP_B]        = &&do_OP_B,
        [OP_BT]       = &&do_OP_BT,
        [OP_BF]       = &&do_OP_BF,
        [OP_PUSHENV]  = &&do_OP_PUSHENV,
        [OP_POPENV]   = &&do_OP_POPENV,
        [OP_PUSH]     = &&do_OP_PUSH,
        [OP_PUSHLOC]  = &&do_OP_PUSHLOC,
        [OP_PUSHGLB]  = &&do_OP_PUSHGLB,
        [OP_PUSHBLTN] = &&do_OP_PUSHBLTN,
        [OP_CALL]     = &&do_OP_CALL,
        [OP_BREAK]    = &&do_OP_BREAK,
    };
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    DISPATCH();
#else
    while (codeEnd > ip) {
#endif"""

    assert old_loop_head in loop_body, "old_loop_head not found in loop_body"
    loop_body = loop_body.replace(old_loop_head, new_loop_head, 1)

    old_switch_head = """#ifdef ENABLE_WAD17
        if (ctx->exception != nullptr) {
#ifdef ENABLE_VM_EXCEPTIONS_LOGS
            logError("VM: Exception thrown! Stack Top is %d\\n", ctx->exceptionHandlerStackTop);
#endif
            if (ctx->exceptionHandlerStackTop == 0) {
                // TODO: When Butterscotch is better, we could have a strict mode that DOES throw a error
                logError("VM: The exception handler frame stack is 0, but we have a pending exception to be dispatched! This would've technically crashed the game in the original runner... or we aren't handling exceptions correctly. We'll swallow the exception and hope for the best... (Exception: %s)\\n", ctx->exception->message);
                free(ctx->exception->message);
                free(ctx->exception);
                ctx->exception = nullptr;
                return RValue_makeUndefined();
            }

            // Restore caller frame
            ExceptionHandlerFrame* exceptionHandlerFrame = &ctx->exceptionHandlerFrameStack[ctx->exceptionHandlerStackTop - 1];

            // Not for us, propagate the exception!
            if (exceptionHandlerFrame->boundToCallDepth != ctx->callDepth) {
#ifdef ENABLE_VM_EXCEPTIONS_LOGS
                logWarn("VM: We wanted %d but we are %d - Propagating...\\n", exceptionHandlerFrame->boundToCallDepth, ctx->callDepth);
#endif
                return RValue_makeUndefined();
            }

            // We DO NOT want to pop the handler, it will be popped by @@try_unhook@@
            bool isException = exceptionHandlerFrame->jumpToOnException != -1;
            ip = isException ? exceptionHandlerFrame->jumpToOnException : exceptionHandlerFrame->jumpToOnSuccess;
            unwindVMStack(ctx, exceptionHandlerFrame->stackTop);

            VM_SYNC_IP();

#ifdef ENABLE_VM_EXCEPTIONS_LOGS
            logWarn("VM: Jumped to %d due to exception handler, is this a exception? %d\\n", ip, isException);
#endif

            if (isException) {
                // On a exception, GameMaker pops the struct and stores it in the variable of the "catch (_nameOfTheExceptionHere) {"
                Instance* gmlStruct = Runner_createStruct(ctx->runner);
                // TODO: longMessage/script/stacktrace
                VM_structSet(ctx, gmlStruct, "message", RValue_makeOwnedString(safeStrdup(ctx->exception->message)), -1);
                stackPush(ctx, RValue_makeStructAndIncRef(gmlStruct));
                free(ctx->exception->message);
                free(ctx->exception);
            } else {
                // Will be unparked by finish_finally
                ctx->parkedException = ctx->exception;
            }

            ctx->exception = nullptr;
        }
#endif

#ifdef ENABLE_VM_GML_PROFILER
        if (ctx->profiler != nullptr)
            Profiler_tickInstruction(ctx->profiler);
#endif
        uint32_t instrAddr = ip;
        uint32_t instr = BinaryUtils_readUint32Aligned(bytecodeBase + ip);
        ip += 4;

        // extraData pointer (may not be used depending on opcode)
        const uint8_t* extraData = bytecodeBase + ip;

        // If instruction has extra data (bit 30 set), advance IP past it
        if (instrHasExtraData(instr)) {
            ip += extraDataSize(instrType1(instr));
        }

        uint8_t opcode = instrOpcode(instr);

#ifdef ENABLE_VM_OPCODE_PROFILER
        if (ctx->opcodeProfilerEnabled) {
            ctx->opcodeCounts[opcode]++;
            ctx->opcodeVariantCounts[opcode * 256 + instrType1(instr) * 16 + instrType2(instr)]++;
            if (opcode == OP_BREAK) {
                int16_t breakType = instrInstanceType(instr);
                int idx = -breakType;
                if (idx >= 0 && 64 > idx) {
                    ctx->breakSubOpCounts[idx]++;
                }
            }
            // Capture actual runtime RValue types for arithmetic/comparison/conversion ops.
            // typeB = 0xF sentinel for unary ops (no second operand).
            uint8_t rvTypeA = 0xFF, rvTypeB = 0xF;
            switch (opcode) {
                case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD:
                case OP_ADD: case OP_SUB: case OP_AND: case OP_OR:
                case OP_XOR: case OP_SHL: case OP_SHR: case OP_CMP:
                    if (ctx->stack.top >= 2) {
                        rvTypeA = slots[ctx->stack.top - 2].type;
                        rvTypeB = slots[ctx->stack.top - 1].type;
                    }
                    break;
                case OP_NEG: case OP_NOT: case OP_CONV:
                    if (ctx->stack.top >= 1) {
                        rvTypeA = slots[ctx->stack.top - 1].type;
                    }
                    break;
            }
            if (rvTypeA != 0xFF) {
                ctx->opcodeRValueTypeCounts[opcode * 256 + (rvTypeA & 0xF) * 16 + (rvTypeB & 0xF)]++;
            }
        }
#endif

#ifdef ENABLE_VM_TRACING
        if (shlen(ctx->opcodesToBeTraced) > 0 && ctx->runner->frameCount >= ctx->traceBytecodeAfterFrame) {
            if (shgeti(ctx->opcodesToBeTraced, "*") != -1 || shgeti(ctx->opcodesToBeTraced, ctx->currentCodeName) != -1) {
                char opcodeStr[32], operandStr[256] = "", commentStr[128] = "";
                formatInstruction(ctx, ctx->bytecodeBase, instrAddr, instr, extraData, opcodeStr, sizeof(opcodeStr), operandStr, sizeof(operandStr), commentStr, sizeof(commentStr));

                char* stackBuf = formatStackContents(ctx);

                if (operandStr[0] != '\\0') {
                    logInfo("VM: [%s] @%04X (%d) [0x%08X] %s %s [stack=%d] %s\\n", ctx->currentCodeName, instrAddr, instrAddr, instr, opcodeStr, operandStr, ctx->stack.top, stackBuf);
                } else {
                    logInfo("VM: [%s] @%04X (%d) [0x%08X] %s [stack=%d] %s\\n", ctx->currentCodeName, instrAddr, instrAddr, instr, opcodeStr, ctx->stack.top, stackBuf);
                }
                free(stackBuf);
            }
        }
#endif

        switch (opcode) {"""

    new_switch_head = """#ifdef ENABLE_WAD17
handle_exception:
        if (ctx->exception != nullptr) {
#ifdef ENABLE_VM_EXCEPTIONS_LOGS
            logError("VM: Exception thrown! Stack Top is %d\\n", ctx->exceptionHandlerStackTop);
#endif
            if (ctx->exceptionHandlerStackTop == 0) {
                // TODO: When Butterscotch is better, we could have a strict mode that DOES throw a error
                logError("VM: The exception handler frame stack is 0, but we have a pending exception to be dispatched! This would've technically crashed the game in the original runner... or we aren't handling exceptions correctly. We'll swallow the exception and hope for the best... (Exception: %s)\\n", ctx->exception->message);
                free(ctx->exception->message);
                free(ctx->exception);
                ctx->exception = nullptr;
                return RValue_makeUndefined();
            }

            // Restore caller frame
            ExceptionHandlerFrame* exceptionHandlerFrame = &ctx->exceptionHandlerFrameStack[ctx->exceptionHandlerStackTop - 1];

            // Not for us, propagate the exception!
            if (exceptionHandlerFrame->boundToCallDepth != ctx->callDepth) {
#ifdef ENABLE_VM_EXCEPTIONS_LOGS
                logWarn("VM: We wanted %d but we are %d - Propagating...\\n", exceptionHandlerFrame->boundToCallDepth, ctx->callDepth);
#endif
                return RValue_makeUndefined();
            }

            // We DO NOT want to pop the handler, it will be popped by @@try_unhook@@
            bool isException = exceptionHandlerFrame->jumpToOnException != -1;
            ip = isException ? exceptionHandlerFrame->jumpToOnException : exceptionHandlerFrame->jumpToOnSuccess;
            unwindVMStack(ctx, exceptionHandlerFrame->stackTop);

            VM_SYNC_IP();

#ifdef ENABLE_VM_EXCEPTIONS_LOGS
            logWarn("VM: Jumped to %d due to exception handler, is this a exception? %d\\n", ip, isException);
#endif

            if (isException) {
                // On a exception, GameMaker pops the struct and stores it in the variable of the "catch (_nameOfTheExceptionHere) {"
                Instance* gmlStruct = Runner_createStruct(ctx->runner);
                // TODO: longMessage/script/stacktrace
                VM_structSet(ctx, gmlStruct, "message", RValue_makeOwnedString(safeStrdup(ctx->exception->message)), -1);
                stackPush(ctx, RValue_makeStructAndIncRef(gmlStruct));
                free(ctx->exception->message);
                free(ctx->exception);
            } else {
                // Will be unparked by finish_finally
                ctx->parkedException = ctx->exception;
            }

            ctx->exception = nullptr;
#if VM_USE_COMPUTED_GOTO
            DISPATCH();
#endif
        }
#endif

#if !VM_USE_COMPUTED_GOTO
        instrAddr = ip;
        instr = BinaryUtils_readUint32Aligned(bytecodeBase + ip);
        ip += 4;

        // extraData pointer (may not be used depending on opcode)
        extraData = bytecodeBase + ip;

        // If instruction has extra data (bit 30 set), advance IP past it
        if (instrHasExtraData(instr)) {
            ip += extraDataSize(instrType1(instr));
        }

        opcode = instrOpcode(instr);
        vm_check_debug(ctx, instrAddr, instr, extraData, opcode, slots);

        switch (opcode) {
#endif"""

    assert old_switch_head in loop_body, "old_switch_head not found in loop_body"
    loop_body = loop_body.replace(old_switch_head, new_switch_head, 1)

    # Convert all 33 opcodes ONLY within loop_body
    opcodes_to_target = [
        "OP_PUSH", "OP_PUSHLOC", "OP_PUSHGLB", "OP_PUSHBLTN", "OP_PUSHI",
        "OP_POP", "OP_POPZ", "OP_ADD", "OP_SUB", "OP_MUL", "OP_DIV",
        "OP_REM", "OP_MOD", "OP_AND", "OP_OR", "OP_XOR", "OP_SHL", "OP_SHR",
        "OP_NEG", "OP_NOT", "OP_CONV", "OP_CMP", "OP_DUP", "OP_B", "OP_BT",
        "OP_BF", "OP_CALL", "OP_CALLV", "OP_RET", "OP_EXIT", "OP_PUSHENV",
        "OP_POPENV", "OP_BREAK"
    ]

    for op in opcodes_to_target:
        pat = f"case {op}:"
        rep = f"TARGET({op}):"
        assert pat in loop_body, f"pat {pat} not found in loop_body"
        loop_body = loop_body.replace(pat, rep, 1)

    # Now convert breaks within loop_body:
    # 1. OP_PUSH
    old_push_breaks = """#endif
                                break;
                            }
                        }
                    }
                }
                handlePush(ctx, instr, extraData, type1);
                break;
            }"""
    new_push_breaks = """#endif
                                DISPATCH();
                            }
                        }
                    }
                }
                handlePush(ctx, instr, extraData, type1);
                DISPATCH();
            }"""
    assert old_push_breaks in loop_body, "old_push_breaks not found"
    loop_body = loop_body.replace(old_push_breaks, new_push_breaks, 1)

    # 2. OP_PUSHLOC
    old_pushloc_breaks = """                    pushTopLevelArrayRef(ctx, &ctx->localVars[localSlot], varType == VARTYPE_ARRAYPOPAF);
                    break;
                }
#endif
                // Locals are always non-builtin (varID >= 0); inline the read straight from localVars[].
                Variable* varDef = resolveVarDef(ctx, varRef);
                uint32_t localSlot = resolveLocalSlot(ctx, varDef->varID);
                require(ctx->localVarCount > localSlot);
                RValue val = ctx->localVars[localSlot];
                val.ownsReference = false;
                stackPushTyped(ctx, val, GML_TYPE_VARIABLE);
#ifdef ENABLE_VM_TRACING
                VM_checkIfVariableShouldBeTracedAndLog(ctx, "local", nullptr, varDef->name, val, false, -1, -1, "");
#endif
                break;
            }"""
    new_pushloc_breaks = """                    pushTopLevelArrayRef(ctx, &ctx->localVars[localSlot], varType == VARTYPE_ARRAYPOPAF);
                    DISPATCH();
                }
#endif
                // Locals are always non-builtin (varID >= 0); inline the read straight from localVars[].
                Variable* varDef = resolveVarDef(ctx, varRef);
                uint32_t localSlot = resolveLocalSlot(ctx, varDef->varID);
                require(ctx->localVarCount > localSlot);
                RValue val = ctx->localVars[localSlot];
                val.ownsReference = false;
                stackPushTyped(ctx, val, GML_TYPE_VARIABLE);
#ifdef ENABLE_VM_TRACING
                VM_checkIfVariableShouldBeTracedAndLog(ctx, "local", nullptr, varDef->name, val, false, -1, -1, "");
#endif
                DISPATCH();
            }"""
    assert old_pushloc_breaks in loop_body, "old_pushloc_breaks not found"
    loop_body = loop_body.replace(old_pushloc_breaks, new_pushloc_breaks, 1)

    # 3. OP_PUSHGLB
    old_pushglb_breaks = """#ifdef ENABLE_VM_TRACING
                                VM_checkIfVariableShouldBeTracedAndLog(ctx, "global", nullptr, varDef->name, val, false, -1, -1, "");
#endif
                                break;
                            }
                        }
                    }
                }
                RValue val = resolveVariableRead(ctx, INSTANCE_GLOBAL, varRef);
                stackPushTyped(ctx, val, GML_TYPE_VARIABLE);
                break;
            }"""
    new_pushglb_breaks = """#ifdef ENABLE_VM_TRACING
                                VM_checkIfVariableShouldBeTracedAndLog(ctx, "global", nullptr, varDef->name, val, false, -1, -1, "");
#endif
                                DISPATCH();
                            }
                        }
                    }
                }
                RValue val = resolveVariableRead(ctx, INSTANCE_GLOBAL, varRef);
                stackPushTyped(ctx, val, GML_TYPE_VARIABLE);
                DISPATCH();
            }"""
    assert old_pushglb_breaks in loop_body, "old_pushglb_breaks not found"
    loop_body = loop_body.replace(old_pushglb_breaks, new_pushglb_breaks, 1)

    # 4. OP_PUSHBLTN & OP_PUSHI
    old_bltn_i = """            TARGET(OP_PUSHBLTN):
                handlePushBltn(ctx, instr, extraData);
                break;
            TARGET(OP_PUSHI):
                handlePushI(ctx, instr);
                break;"""
    new_bltn_i = """            TARGET(OP_PUSHBLTN):
                handlePushBltn(ctx, instr, extraData);
                DISPATCH();
            TARGET(OP_PUSHI):
                handlePushI(ctx, instr);
                DISPATCH();"""
    assert old_bltn_i in loop_body, "old_bltn_i not found"
    loop_body = loop_body.replace(old_bltn_i, new_bltn_i, 1)

    # 5. OP_POP & OP_POPZ
    old_pop_popz = """                } else {
                    handlePop(ctx, type1, type2, varRef, varType, instanceType);
                }
                break;
            }
            TARGET(OP_POPZ):
                handlePopz(ctx);
                break;"""
    new_pop_popz = """                } else {
                    handlePop(ctx, type1, type2, varRef, varType, instanceType);
                }
                DISPATCH();
            }
            TARGET(OP_POPZ):
                handlePopz(ctx);
                DISPATCH();"""
    assert old_pop_popz in loop_body, "old_pop_popz not found"
    loop_body = loop_body.replace(old_pop_popz, new_pop_popz, 1)

    # 6. OP_ADD
    old_add = """                    if (a.type == RVALUE_STRING || b.type == RVALUE_STRING) {
                        handleAddString(ctx, a, b, resultType);
                        break;
                    }
#ifndef NO_RVALUE_INT64
                    if (a.type == RVALUE_INT64 && b.type == RVALUE_INT64) {
                        stackPushTyped(ctx, RValue_makeInt64(a.int64 + b.int64), resultType);
                        break;
                    }
#endif
                    GMLReal result = RValue_toReal(a) + RValue_toReal(b);
                    RValue_free(&a);
                    RValue_free(&b);
                    stackPushTyped(ctx, RValue_makeReal(result), resultType);
                }
                break;
            }"""
    new_add = """                    if (a.type == RVALUE_STRING || b.type == RVALUE_STRING) {
                        handleAddString(ctx, a, b, resultType);
                        DISPATCH();
                    }
#ifndef NO_RVALUE_INT64
                    if (a.type == RVALUE_INT64 && b.type == RVALUE_INT64) {
                        stackPushTyped(ctx, RValue_makeInt64(a.int64 + b.int64), resultType);
                        DISPATCH();
                    }
#endif
                    GMLReal result = RValue_toReal(a) + RValue_toReal(b);
                    RValue_free(&a);
                    RValue_free(&b);
                    stackPushTyped(ctx, RValue_makeReal(result), resultType);
                }
                DISPATCH();
            }"""
    assert old_add in loop_body, "old_add not found"
    loop_body = loop_body.replace(old_add, new_add, 1)

    # 7. OP_SUB
    old_sub = """#ifndef NO_RVALUE_INT64
                    if (a.type == RVALUE_INT64 && b.type == RVALUE_INT64) {
                        stackPushTyped(ctx, RValue_makeInt64(a.int64 - b.int64), resultType);
                        break;
                    }
#endif
                    GMLReal result = RValue_toReal(a) - RValue_toReal(b);
                    RValue_free(&a);
                    RValue_free(&b);
                    stackPushTyped(ctx, RValue_makeReal(result), resultType);
                }
                break;
            }"""
    new_sub = """#ifndef NO_RVALUE_INT64
                    if (a.type == RVALUE_INT64 && b.type == RVALUE_INT64) {
                        stackPushTyped(ctx, RValue_makeInt64(a.int64 - b.int64), resultType);
                        DISPATCH();
                    }
#endif
                    GMLReal result = RValue_toReal(a) - RValue_toReal(b);
                    RValue_free(&a);
                    RValue_free(&b);
                    stackPushTyped(ctx, RValue_makeReal(result), resultType);
                }
                DISPATCH();
            }"""
    assert old_sub in loop_body, "old_sub not found"
    loop_body = loop_body.replace(old_sub, new_sub, 1)

    # 8. OP_MUL
    old_mul = """                    if (a.type == RVALUE_STRING) {
                        handleMulString(ctx, a, b, resultType);
                        break;
                    }
#ifndef NO_RVALUE_INT64
                    if (a.type == RVALUE_INT64 && b.type == RVALUE_INT64) {
                        stackPushTyped(ctx, RValue_makeInt64(a.int64 * b.int64), resultType);
                        break;
                    }
#endif
                    GMLReal result = RValue_toReal(a) * RValue_toReal(b);
                    RValue_free(&a);
                    RValue_free(&b);
                    stackPushTyped(ctx, RValue_makeReal(result), resultType);
                }
                break;
            }"""
    new_mul = """                    if (a.type == RVALUE_STRING) {
                        handleMulString(ctx, a, b, resultType);
                        DISPATCH();
                    }
#ifndef NO_RVALUE_INT64
                    if (a.type == RVALUE_INT64 && b.type == RVALUE_INT64) {
                        stackPushTyped(ctx, RValue_makeInt64(a.int64 * b.int64), resultType);
                        DISPATCH();
                    }
#endif
                    GMLReal result = RValue_toReal(a) * RValue_toReal(b);
                    RValue_free(&a);
                    RValue_free(&b);
                    stackPushTyped(ctx, RValue_makeReal(result), resultType);
                }
                DISPATCH();
            }"""
    assert old_mul in loop_body, "old_mul not found"
    loop_body = loop_body.replace(old_mul, new_mul, 1)

    # 9. Simple arithmetic / bitwise
    old_simple = """            TARGET(OP_DIV): handleDiv(ctx, instr); break;
            TARGET(OP_REM): handleRem(ctx, instr); break;
            TARGET(OP_MOD): handleMod(ctx, instr); break;

            // Bitwise / Logical
            TARGET(OP_AND): handleAnd(ctx, instr); break;
            TARGET(OP_OR):  handleOr(ctx, instr);  break;
            TARGET(OP_XOR): handleXor(ctx, instr); break;
            TARGET(OP_SHL): handleShl(ctx, instr); break;
            TARGET(OP_SHR): handleShr(ctx, instr); break;

            // Unary
            TARGET(OP_NEG): handleNeg(ctx, instr); break;
            TARGET(OP_NOT): handleNot(ctx, instr); break;"""
    new_simple = """            TARGET(OP_DIV): handleDiv(ctx, instr); DISPATCH();
            TARGET(OP_REM): handleRem(ctx, instr); DISPATCH();
            TARGET(OP_MOD): handleMod(ctx, instr); DISPATCH();

            // Bitwise / Logical
            TARGET(OP_AND): handleAnd(ctx, instr); DISPATCH();
            TARGET(OP_OR):  handleOr(ctx, instr);  DISPATCH();
            TARGET(OP_XOR): handleXor(ctx, instr); DISPATCH();
            TARGET(OP_SHL): handleShl(ctx, instr); DISPATCH();
            TARGET(OP_SHR): handleShr(ctx, instr); DISPATCH();

            // Unary
            TARGET(OP_NEG): handleNeg(ctx, instr); DISPATCH();
            TARGET(OP_NOT): handleNot(ctx, instr); DISPATCH();"""
    assert old_simple in loop_body, "old_simple not found"
    loop_body = loop_body.replace(old_simple, new_simple, 1)

    # 10. OP_CONV
    old_conv_end = """                if (fastHit) {
                    top->gmlStackType = dstType;
                } else {
                    handleConv(ctx, srcType, dstType, convKey);
                }
                break;
            }"""
    new_conv_end = """                if (fastHit) {
                    top->gmlStackType = dstType;
                } else {
                    handleConv(ctx, srcType, dstType, convKey);
                }
                DISPATCH();
            }"""
    assert old_conv_end in loop_body, "old_conv_end not found"
    loop_body = loop_body.replace(old_conv_end, new_conv_end, 1)

    # 11. OP_CMP
    old_cmp_end = """                    slotA->int32 = result ? 1 : 0;
                    slotA->type = RVALUE_BOOL;
                    slotA->gmlStackType = GML_TYPE_BOOL;
                    ctx->stack.top--;
                } else {
                    handleCmp(ctx, instr);
                }
                break;
            }"""
    new_cmp_end = """                    slotA->int32 = result ? 1 : 0;
                    slotA->type = RVALUE_BOOL;
                    slotA->gmlStackType = GML_TYPE_BOOL;
                    ctx->stack.top--;
                } else {
                    handleCmp(ctx, instr);
                }
                DISPATCH();
            }"""
    assert old_cmp_end in loop_body, "old_cmp_end not found"
    loop_body = loop_body.replace(old_cmp_end, new_cmp_end, 1)

    # 12. OP_DUP
    old_dup = """            TARGET(OP_DUP):
                handleDup(ctx, instr);
                break;"""
    new_dup = """            TARGET(OP_DUP):
                handleDup(ctx, instr);
                DISPATCH();"""
    assert old_dup in loop_body, "old_dup not found"
    loop_body = loop_body.replace(old_dup, new_dup, 1)

    # 13. Branches
    old_branches = """            TARGET(OP_B): {
                int32_t offset = instrJumpOffset(instr);
                ip = instrAddr + offset;
                break;
            }
            TARGET(OP_BT): {
                bool condition = stackPopInt32(ctx) != 0;
                if (condition == true) {
                    int32_t offset = instrJumpOffset(instr);
                    ip = instrAddr + offset;
                }
                break;
            }
            TARGET(OP_BF): {
                bool condition = stackPopInt32(ctx) != 0;
                if (condition == false) {
                    int32_t offset = instrJumpOffset(instr);
                    ip = instrAddr + offset;
                }
                break;
            }"""
    new_branches = """            TARGET(OP_B): {
                int32_t offset = instrJumpOffset(instr);
                ip = instrAddr + offset;
                DISPATCH();
            }
            TARGET(OP_BT): {
                bool condition = stackPopInt32(ctx) != 0;
                if (condition == true) {
                    int32_t offset = instrJumpOffset(instr);
                    ip = instrAddr + offset;
                }
                DISPATCH();
            }
            TARGET(OP_BF): {
                bool condition = stackPopInt32(ctx) != 0;
                if (condition == false) {
                    int32_t offset = instrJumpOffset(instr);
                    ip = instrAddr + offset;
                }
                DISPATCH();
            }"""
    assert old_branches in loop_body, "old_branches not found"
    loop_body = loop_body.replace(old_branches, new_branches, 1)

    # 14. Calls, Env, Break, Fallthrough
    old_calls_env_end = """            // Function call
            TARGET(OP_CALL):
                VM_SYNC_IP();
                handleCall(ctx, instr, extraData);
                break;
#if IS_WAD17_OR_HIGHER_ENABLED
            TARGET(OP_CALLV):
                VM_SYNC_IP();
                handleCallV(ctx, instr);
                break;
#endif

            // Return
            TARGET(OP_RET): {
                RValue retVal = stackPop(ctx);
                return retVal;
            }

            // Exit (no return value)
            TARGET(OP_EXIT):
                return scriptFallthroughReturnValue(ctx);

            // Environment (with-statements)
            TARGET(OP_PUSHENV):
                VM_SYNC_IP();
                handlePushEnv(ctx, instr, instrAddr);
                VM_RELOAD_IP();
                break;
            TARGET(OP_POPENV):
                VM_SYNC_IP();
                handlePopEnv(ctx, instr, instrAddr);
                VM_RELOAD_IP();
                break;

            // Break (extended opcodes in V17+, no-op/debug in V16)
            TARGET(OP_BREAK):
#if IS_WAD17_OR_HIGHER_ENABLED
                handleBreak(ctx, instr, instrAddr, extraData);
#endif
                break;

            default:
                logError("VM: Unknown opcode 0x%02X at offset %u\\n", opcode, instrAddr);
                abort();
        }
    }

    return scriptFallthroughReturnValue(ctx);
}"""

    new_calls_env_end = """            // Function call
            TARGET(OP_CALL):
                VM_SYNC_IP();
                handleCall(ctx, instr, extraData);
                DISPATCH();

            TARGET(OP_CALLV):
#if IS_WAD17_OR_HIGHER_ENABLED
                VM_SYNC_IP();
                handleCallV(ctx, instr);
                DISPATCH();
#else
#if VM_USE_COMPUTED_GOTO
                goto do_unknown;
#else
                logError("VM: Unknown opcode 0x%02X at offset %u\\n", opcode, instrAddr);
                abort();
#endif
#endif

            // Return
            TARGET(OP_RET): {
                RValue retVal = stackPop(ctx);
                return retVal;
            }

            // Exit (no return value)
            TARGET(OP_EXIT):
                return scriptFallthroughReturnValue(ctx);

            // Environment (with-statements)
            TARGET(OP_PUSHENV):
                VM_SYNC_IP();
                handlePushEnv(ctx, instr, instrAddr);
                VM_RELOAD_IP();
                DISPATCH();
            TARGET(OP_POPENV):
                VM_SYNC_IP();
                handlePopEnv(ctx, instr, instrAddr);
                VM_RELOAD_IP();
                DISPATCH();

            // Break (extended opcodes in V17+, no-op/debug in V16)
            TARGET(OP_BREAK):
#if IS_WAD17_OR_HIGHER_ENABLED
                handleBreak(ctx, instr, instrAddr, extraData);
#endif
                DISPATCH();

#if !VM_USE_COMPUTED_GOTO
            default:
                logError("VM: Unknown opcode 0x%02X at offset %u\\n", opcode, instrAddr);
                abort();
        }
    }
#else
    do_unknown:
        logError("VM: Unknown opcode 0x%02X at offset %u\\n", opcode, instrAddr);
        abort();
#endif

loop_end:
    return scriptFallthroughReturnValue(ctx);
}

#undef TARGET
#undef DISPATCH"""

    assert old_calls_env_end in loop_body, "old_calls_env_end not found"
    loop_body = loop_body.replace(old_calls_env_end, new_calls_env_end, 1)

    final_content = pre_loop + loop_body + post_loop

    with open(vm_path, "wb") as f:
        f.write(final_content.encode("utf-8"))
    print("Successfully transformed executeLoop in butterscotch/src/vm.c!")

if __name__ == "__main__":
    build_vm_code()
