#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <folly/io/IOBuf.h>
#include <proxygen/httpserver/ResponseBuilder.h>

namespace proxygen {
    class ResponseBuilder;
    class HTTPMessage;
}

namespace ModuleManage {
    enum class HookStage : uint8_t {
        PRE_REQUEST = 0,
        PRE_RESPONSE = 1,
        POST_RESPONSE = 2,
        HOOK_STAGE_COUNT = 3
    };

    enum class ModuleResult : uint8_t {
        CONTINUE = 0,
        BREAK = 1,
    };

    struct ModuleContext {
        folly::fbstring document_root;
        folly::fbstring file_path;
        uint64_t file_path_hash;
        std::shared_ptr<folly::IOBuf> request_body;
        std::unique_ptr<proxygen::HTTPMessage> request;
        std::unique_ptr<proxygen::ResponseBuilder> response;

        ~ModuleContext() noexcept {
        }

        inline bool hasRequestBody() const noexcept {
            return request_body && !request_body->empty();
        }

        inline folly::fbstring getRequestBody() const noexcept {
            if (!hasRequestBody()) {
                return {};
            }
            return request_body->moveToFbString();
        }

        inline size_t getRequestBodySize() const noexcept {
            return request_body ? request_body->computeChainDataLength() : 0;
        }
    };

    using ModuleHook = ModuleResult(*)(ModuleContext &);

    struct Module {
        const char *name;
        const char *version;
        uint32_t priority; // Lower number = higher priority
        bool enabled;

        ModuleHook pre_request_hook;
        ModuleHook pre_response_hook;
        ModuleHook post_response_hook;

        bool (*init)(void);

        void (*cleanup)(void);
    };

    template<size_t MAX_MODULES = 32>
    class System {
    private:
        alignas(64) std::array<Module, MAX_MODULES> modules_;
        alignas(64) std::array<std::array<uint8_t, MAX_MODULES>, static_cast<size_t>(HookStage::HOOK_STAGE_COUNT)>
        execution_order_;
        alignas(64) std::array<size_t, static_cast<size_t>(HookStage::HOOK_STAGE_COUNT)> hook_count_;
        size_t module_count_;

        inline void sort_modules() noexcept;

        constexpr inline ModuleHook get_hook_direct(const Module &module, HookStage stage) noexcept;

    public:
        System() noexcept : module_count_(0) {
            hook_count_.fill(0);
        }

        inline bool register_module(const Module &module) noexcept;

        bool initialize() noexcept;

        void cleanup() noexcept;

        [[gnu::hot]] [[gnu::flatten]]
        inline ModuleResult execute_hooks(HookStage stage, ModuleContext &ctx) noexcept;
    };
} // namespace ModuleManage

inline ModuleManage::System<32> g_moduleSystem;

#define REGISTER_MODULE(module) \
static ModuleManage::Module* __module_##module __attribute__((used, section("my_module_section"))) = &module;
