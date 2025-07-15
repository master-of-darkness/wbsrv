// module_system.cpp
#include "module.h"

#include <folly/logging/xlog.h>
#include <proxygen/httpserver/ResponseBuilder.h>

namespace ModuleManage {
    template<size_t MAX_MODULES>
    inline void System<MAX_MODULES>::sort_modules() noexcept {
        Module *__restrict__ modules = modules_.data();

        for (size_t stage = 0; stage < static_cast<size_t>(HookStage::HOOK_STAGE_COUNT); ++stage) {
            hook_count_[stage] = 0;
            uint8_t *__restrict__ order = execution_order_[stage].data();

            for (size_t i = 0; i < module_count_; ++i) {
                if (modules[i].enabled && get_hook_direct(modules[i], static_cast<HookStage>(stage))) {
                    order[hook_count_[stage]] = static_cast<uint8_t>(i);
                    hook_count_[stage]++;
                }
            }

            const size_t count = hook_count_[stage];
            if (count <= 1) continue;

            for (size_t i = 1; i < count; ++i) {
                const uint8_t key = order[i];
                const uint32_t key_priority = modules[key].priority;
                size_t j = i;

                while (j > 0 && modules[order[j - 1]].priority > key_priority) {
                    order[j] = order[j - 1];
                    j--;
                }
                order[j] = key;
            }
        }
    }

    template<size_t MAX_MODULES>
    constexpr inline ModuleHook System<MAX_MODULES>::get_hook_direct(const Module &module, HookStage stage) noexcept {
        switch (stage) {
            case HookStage::PRE_REQUEST: return module.pre_request_hook;
            case HookStage::PRE_RESPONSE: return module.pre_response_hook;
            case HookStage::POST_RESPONSE: return module.post_response_hook;
            default: return nullptr;
        }
    }

    template<size_t MAX_MODULES>
    inline bool System<MAX_MODULES>::register_module(const Module &module) noexcept {
        if (module_count_ >= MAX_MODULES) [[unlikely]] {
            return false;
        }

        modules_[module_count_] = module;
        module_count_++;
        return true;
    }

    template<size_t MAX_MODULES>
    bool System<MAX_MODULES>::initialize() noexcept {
        const Module *__restrict__ modules = modules_.data();

        for (size_t i = 0; i < module_count_; ++i) {
            if (modules[i].init && !modules[i].init()) [[unlikely]] {
                return false;
            }
        }

        sort_modules();
        return true;
    }

    template<size_t MAX_MODULES>
    void System<MAX_MODULES>::cleanup() noexcept {
        const Module *__restrict__ modules = modules_.data();

        for (size_t i = 0; i < module_count_; ++i) {
            if (modules[i].cleanup) [[likely]] {
                modules[i].cleanup();
            }
        }
    }

    template<size_t MAX_MODULES>
    [[gnu::hot]] [[gnu::flatten]]
    inline ModuleResult System<MAX_MODULES>::execute_hooks(HookStage stage, ModuleContext &ctx) noexcept {
        const size_t stage_idx = static_cast<size_t>(stage);
        const size_t count = hook_count_[stage_idx];

        // Early exit for empty hook lists
        if (count == 0) [[unlikely]] {
            return ModuleResult::CONTINUE;
        }

        // Use restrict pointers for better optimization
        const uint8_t *__restrict__ order = execution_order_[stage_idx].data();
        const Module *__restrict__ modules = modules_.data();

        // Unroll small loops for better performance
        if (count <= 4) {
            for (size_t i = 0; i < count; ++i) {
                const uint8_t idx = order[i];
                const ModuleHook hook = get_hook_direct(modules[idx], stage);

                const ModuleResult result = hook(ctx);
                if (result != ModuleResult::CONTINUE) [[unlikely]] {
                    return result;
                }
            }
        } else {
            // Standard loop for larger hook counts
            for (size_t i = 0; i < count; ++i) {
                const uint8_t idx = order[i];
                const ModuleHook hook = get_hook_direct(modules[idx], stage);

                const ModuleResult result = hook(ctx);
                if (result != ModuleResult::CONTINUE) [[unlikely]] {
                    return result;
                }
            }
        }

        return ModuleResult::CONTINUE;
    }

    template class System<32>;
} // namespace ModuleManage
