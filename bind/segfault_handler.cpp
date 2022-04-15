// Copyright 2022 Guanyu Feng and Shengqi Chen, Tsinghua University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cache.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <errno.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#ifdef __x86_64__
#include <fadec.h>
#endif

namespace
{
#ifdef __x86_64__
    constexpr size_t MAX_INSTR_LENGTH = 15, MAX_OP_SIZE = 4, MAX_REG_SIZE = 16;
    constexpr size_t FADEC_REG_TO_GNU_REG[MAX_REG_SIZE] = {13, 14, 12, 11, 15, 10, 9, 8, 0, 1, 2, 3, 4, 5, 6, 7};
    struct trap_context_type
    {
        ucontext_t ucontext;
        char *exec_buffer = nullptr;
        bool under_trap = false;
        int instr_length;
        bool modified[MAX_REG_SIZE];
    };
    thread_local trap_context_type trap_context = trap_context_type();
#endif

    void print_trace()
    {
        void *trace[5];

        int trace_size = backtrace(trace, 3);
        char **messages = backtrace_symbols(trace, trace_size);
        if (messages)
        {
            for (int i = 1; i < trace_size; i++)
                printf("  [%02d] %s\n", i, messages[i]);
            free(messages);
        }
    }

    void segfault_sigaction(int signal, siginfo_t *si, void *arg)
    {
        // fprintf(stderr, "Caught segfault at address %p\n", si->si_addr);

        if (trap_context.under_trap)
        {
            fprintf(stderr, "Nested SIGSEGV\n");
            abort();
        }
        else
        {
            trap_context.under_trap = true;
        }

        ucontext_t *context = (ucontext_t *)arg;
        trap_context.ucontext = *context;

        if (!trap_context.exec_buffer)
        {
            trap_context.exec_buffer =
                (char *)mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            if (trap_context.exec_buffer == MAP_FAILED)
            {
                fprintf(stderr, "mmap %s\n", strerror(errno));
                abort();
            }
        }

#ifdef __x86_64__
        const uint8_t *instr_pointer = (const uint8_t *)context->uc_mcontext.gregs[REG_RIP];
        // fprintf(stderr, "Address from where crash happen is %p\n", instr_pointer);
        // print_trace();

        FdInstr instr;
        char fmt[128];

        int decode_ret = fd_decode(instr_pointer, MAX_INSTR_LENGTH, 64, 0, &instr);

        // if (decode_ret == FD_ERR_INTERNAL) {
        //     return;
        // } else if (decode_ret == FD_ERR_PARTIAL) {
        //     strcpy(fmt, "PARTIAL");
        // } else if (decode_ret == FD_ERR_UD) {
        //     strcpy(fmt, "UD");
        // } else {
        //     fd_format(&instr, fmt, sizeof(fmt));
        // }

        // fprintf(stderr, "Decoded instruction is %s\n", fmt);

        trap_context.instr_length = decode_ret;
        memcpy(trap_context.exec_buffer, instr_pointer, trap_context.instr_length);
        trap_context.exec_buffer[trap_context.instr_length] = '\xcc';
        memset(trap_context.modified, 0, sizeof(trap_context.modified));

        context->uc_mcontext.gregs[REG_RIP] = (uint64_t)trap_context.exec_buffer;

        for (size_t i = 0; i < MAX_OP_SIZE; i++)
        {
            FdOpType op_type = FD_OP_TYPE(&instr, i);
            if (op_type == FD_OT_NONE)
                break;

            if (op_type == FD_OT_MEM)
            {
                bool has_base = FD_OP_BASE(&instr, i) != FD_REG_NONE;
                if (has_base)
                {
                    uint8_t *ptr = nullptr;

                    uint64_t base = context->uc_mcontext.gregs[FADEC_REG_TO_GNU_REG[FD_OP_BASE(&instr, i)]];
                    int64_t offset = 0;
                    if (FD_ADDRSIZE(&instr) == 2)
                        base &= 0xffff;
                    else if (FD_ADDRSIZE(&instr) == 4)
                        base &= 0xffffffff;

                    bool has_idx = FD_OP_INDEX(&instr, i) != FD_REG_NONE;
                    if (has_idx)
                    {
                        offset = context->uc_mcontext.gregs[FADEC_REG_TO_GNU_REG[FD_OP_INDEX(&instr, i)]];
                        if (FD_ADDRSIZE(&instr) == 2)
                            offset &= 0xffff;
                        else if (FD_ADDRSIZE(&instr) == 4)
                            offset &= 0xffffffff;
                        offset *= (1 << FD_OP_SCALE(&instr, i));
                    }

                    int64_t disp = FD_OP_DISP(&instr, i);
                    if (disp)
                    {
                        offset += disp;
                    }

                    ptr = (uint8_t *)base + offset;

                    // fprintf(stderr, "Address memory access from %p \n", ptr);

                    if (cache_space_ptr(ptr))
                    {
                        const void *raw_ptr = nullptr;
                        // TODO: use read for readonly instr
                        if (true) // (i == 0)
                            raw_ptr = cache_get_raw_ptr_store(ptr);
                        else
                            raw_ptr = cache_get_raw_ptr_load(ptr);

                        raw_ptr = (const uint8_t *)raw_ptr - offset;

                        context->uc_mcontext.gregs[FADEC_REG_TO_GNU_REG[FD_OP_BASE(&instr, i)]] =
                            (unsigned long long)(raw_ptr);
                        trap_context.modified[FADEC_REG_TO_GNU_REG[FD_OP_BASE(&instr, i)]] = true;
                    }
                }
            }
        }
#endif
    }

    void trap_sigaction(int signal, siginfo_t *si, void *arg)
    {
        // fprintf(stderr, "Caught trap at address %p\n", si->si_addr);

        if (!trap_context.under_trap)
        {
            fprintf(stderr, "Not inited trap\n");
            abort();
        }
        else
        {
            trap_context.under_trap = false;
        }

        ucontext_t *new_context = (ucontext_t *)arg;

        for (size_t i = 0; i < MAX_REG_SIZE; i++)
        {
            if (trap_context.modified[i])
            {
                new_context->uc_mcontext.gregs[i] = trap_context.ucontext.uc_mcontext.gregs[i];
            }
        }

#ifdef __x86_64__
        new_context->uc_mcontext.gregs[REG_RIP] =
            trap_context.ucontext.uc_mcontext.gregs[REG_RIP] + trap_context.instr_length;
#endif
    }
} // namespace

void cache_segfault_handler_init()
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_sigaction;
    sa.sa_flags = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);

    sa.sa_sigaction = trap_sigaction;
    sigaction(SIGTRAP, &sa, NULL);
}
