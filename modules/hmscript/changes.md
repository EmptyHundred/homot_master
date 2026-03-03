# HMScript 模块改动记录

> 用于跟踪 HMScript 及其沙盒相关对引擎核心（尤其是 GDScript）所做的所有改动，便于后续升级同步与回归。

## 2026-02-25

- **新增 `sandbox` 子目录（仅影响 `modules/hmscript`）**
  - 添加 `sandbox/sandbox_config.h/.cpp`：实现 `HMSandboxConfig`，提供：
    - 默认安全策略（类 / 方法 / 属性 blocklist、路径白名单）。
    - 从 JSON 文件加载自定义策略的能力。
  - 添加 `sandbox/sandbox_limiter.h/.cpp`：实现 `HMSandboxLimiter`，提供：
    - 执行超时、内存上限。
    - READ/WRITE/HEAVY 分级 API 调用配额与每帧重置。
  - 添加 `sandbox/sandbox_error.h/.cpp`：实现 `HMSandboxErrorEntry` 与 `HMSandboxErrorRegistry`，用于：
    - 结构化记录错误（类型、严重度、位置、堆栈、上下文、阶段、次数）。
    - 导出错误数组与 Markdown 报告。
  - 添加 `sandbox/sandbox_runtime.h/.cpp`：实现 `HMSandbox`，用于：
    - 聚合配置、限流与错误仓库。
    - 提供受控的脚本函数调用入口 `call_script_function`，在调用前后检查超时与内存，并记录错误。

- **构建脚本更新**
  - 更新 `modules/hmscript/SCsub`：
    - 在原有 `env_hmscript.add_source_files(env.modules_sources, "*.cpp")` 基础上，追加编译 `sandbox/*.cpp`。

- **GDScript 与 HMScript 集成（沙盒帧回调）**
  - 修改 `modules/gdscript/gdscript.h` 与 `modules/gdscript/gdscript.cpp`：
    - 在 `GDScriptLanguage` 中新增成员函数指针 `sandbox_frame_callback`，并提供 `set_sandbox_frame_callback(void (*)(void))` 接口。
    - 在 `GDScriptLanguage::frame()` 末尾调用已注册的 `sandbox_frame_callback`（如不为 `nullptr`），用于让外部模块（例如 HMScript 沙盒）在每帧执行限流重置等逻辑。
    - 说明：此改动为可选 hook，对普通 GDScript 行为无影响，如未注册回调则不执行任何额外逻辑。
  - 修改 `modules/hmscript/hmscript_language.h/.cpp`：
    - 前向声明 `hmsandbox::HMSandbox` 并新增 `static HMSandbox *HMScriptLanguage::get_sandbox_runtime()` 便于其他模块访问 HMSandbox 全局运行时。
    - 在 `hmscript_language.cpp` 中包含 `sandbox/sandbox_runtime.h`，创建全局 `HMSandbox g_hm_sandbox_runtime`。
    - 实现 `_hm_sandbox_frame_callback()`，每帧调用 `g_hm_sandbox_runtime.reset_frame_counters()`。
    - 在 `HMScriptLanguage::init()` 中，若 `GDScriptLanguage::get_singleton()` 存在，则调用其 `set_sandbox_frame_callback()` 注册上述帧回调。
    - 说明：当前阶段仅接通“每帧重置”路径，后续可在此基础上进一步扩展更细粒度的沙盒管控（如 API 级限流与错误收集）。

- **HMSandbox 单元测试**
  - 新增目录 `modules/hmscript/tests/`，并在 `SCsub` 中：
    - 当 `env["tests"]` 为真时，编译 `tests/*.cpp` 与 `tests/*/*.cpp`，对齐其他模块（如 `jsonrpc`）的测试集成方式。
  - 为当前已实现的沙盒组件每个创建一个子目录与对应测试：
    - `tests/sandbox_config/test_sandbox_config.cpp`：
      - 覆盖默认类 / 方法 blocklist 是否生效（如 `OS` / `FileAccess` 等被屏蔽）。
      - 覆盖路径白名单与目录穿越检测（允许 `res://` / `user://`，拒绝含 `..` 的危险路径）。
    - `tests/sandbox_limiter/test_sandbox_limiter.cpp`：
      - 验证 WRITE / HEAVY 操作的每帧配额与 `reset_frame_counters()` 行为。
      - 验证内存上限配置与 `is_memory_limit_exceeded()` 的基本判定逻辑。
    - `tests/sandbox_error/test_sandbox_error.cpp`：
      - 验证相同错误多次上报时会去重，并正确累加 `occurrence_count`。
      - 验证 `get_error_report_markdown()` 生成的报告包含关键字段（类型、消息、次数等）。
    - `tests/sandbox_runtime/test_sandbox_runtime.cpp`：
      - 验证 `call_script_function()` 在脚本 / owner 无效或超出内存限制时，会返回 `Variant::NIL` 并记录错误。
  - 测试实现依赖 `tests/test_macros.h`（doctest 宏），可通过 `godot --test` 运行。

- **说明**
  - 本文件会持续记录对 HMScript 模块自身以及与之相关的核心模块（尤其是 `modules/gdscript`）所做的改动，方便后续与上游 Godot 的差异比对与升级迁移。
  - 每次修改需要注明：
    - 修改日期
    - 受影响文件
    - 目的与影响范围（尤其是与上游 Godot 的差异点）

## 2026-02-25（续）

- **在 GDScript 模块中直接复用 HMSandbox “安全与限流” 组件**
  - 修改 `modules/gdscript/gdscript.h`：
    - 引入 `modules/hmscript/sandbox/sandbox_config.h`、`sandbox_limiter.h`、`sandbox_error.h`，以便在 GDScript 层直接复用 HMSandbox 已实现的安全策略与限流逻辑。
    - 在 `GDScriptLanguage` 内新增内部结构体 `SandboxProfile`，聚合：
      - `hmsandbox::HMSandboxConfig config;`
      - `hmsandbox::HMSandboxLimiter limiter;`
      - `hmsandbox::HMSandboxErrorRegistry errors;`
    - 为 `GDScriptLanguage` 新增沙盒管理 API：
      - `SandboxProfile *get_sandbox_profile(const String &p_id);`
      - `SandboxProfile *ensure_sandbox_profile(const String &p_id);`
      - `void reset_sandbox_profiles_per_frame();`
      - `Dictionary get_sandbox_errors(const String &p_id) const;`
      - `String get_sandbox_error_report(const String &p_id) const;`
    - 说明：这些 API 为 HMScript 或未来的 GDScript 沙盒管理器提供了统一入口，可按字符串 ID 管理多套独立的安全配置与限流状态，并查询聚合错误报告。
  - 修改 `modules/gdscript/gdscript.cpp`：
    - 实现上述 `SandboxProfile` 管理函数，内部完全复用 HMSandbox 的 `HMSandboxConfig` / `HMSandboxLimiter` / `HMSandboxErrorRegistry`，不在 GDScript 中重复实现安全与限流算法。
    - 在 `GDScriptLanguage::frame()` 中调用 `reset_sandbox_profiles_per_frame()`，确保所有已注册 GDScript 沙盒 profile 的写操作 / 重操作配额在每帧自动重置，为“强沙盒模式”的限流行为提供基础设施。
    - 说明：现阶段仅提供“安全策略 + 限流 + 错误聚合”的数据结构与生命周期管理，具体在 GDScript VM 调用路径中挂接 `SafeWrapper` 的工作留待后续迭代。

- **为 HMScript 的 `.hm` / `.hmc` 脚本启用 GDScript 沙盒标记**
  - 修改 `modules/gdscript/gdscript.h` 与 `modules/gdscript/gdscript.cpp`：
    - 在 `GDScript` 中新增成员：
      - `bool sandbox_enabled = false;`
      - `String sandbox_profile_id;`
    - 新增接口：
      - `void set_sandbox_enabled(bool p_enabled, const String &p_profile_id = String());`
      - `bool is_sandbox_enabled() const;`
      - `String get_sandbox_profile_id() const;`
    - 在 `GDScript::_create_instance` 中，将脚本上的 `sandbox_enabled` 与 `sandbox_profile_id` 拷贝到对应的 `GDScriptInstance` 上，便于后续在 VM 调用路径中根据实例判断是否需要走沙盒安全检查与限流。
  - 修改 `modules/hmscript/hmscript_language.h/.cpp` 中的 `ResourceFormatLoaderHMScript`：
    - 覆写 `load()`，在调用基础的 `ResourceFormatLoaderGDScript::load()` 获得 `GDScript` 资源后，统一对 `.hm` / `.hmc` 脚本调用：
      - `gds->set_sandbox_enabled(true, "hm_default");`
    - 说明：
      - 这样所有 HMScript 脚本在底层都会被标记为启用 GDScript 沙盒，并挂到 ID 为 `"hm_default"` 的 sandbox profile 上。
      - 上层可以通过 `GDScriptLanguage::ensure_sandbox_profile("hm_default")` 配置默认的安全策略与限流参数，并在未来的 VM hook 中依据实例的 `sandbox_enabled` / `sandbox_profile_id` 做真正的 API 封锁与限流决策。

- **将 GDScript VM 中所有 native 调用路径接入 HMSandbox 安全策略**
  - 修改 `modules/gdscript/gdscript_vm.cpp`：
    - 在 GDScript VM 中已存在的沙盒辅助函数 `_gdscript_sandbox_check_method_bind(...)` 基础上，补全所有 `MethodBind` 调用指令对沙盒的接入，确保 **启用沙盒的 GDScript/HMScript 实例** 在调用引擎原生 API 时都会经过同一套安全与限流检查：
      - 对实例方法调用：
        - 在 `OPCODE_CALL_METHOD_BIND` / `OPCODE_CALL_METHOD_BIND_RET` 中，调用前检查：
          - 若目标对象所属类或其父类命中 `HMSandboxConfig::blocked_classes`；
          - 或方法签名命中 `HMSandboxConfig::blocked_methods`（含继承检索），则拒绝调用并通过 `SandboxProfile::errors` 记录一条 `type="security"` 的错误。
        - 在 `OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN` / `OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN` 中，补充相同的沙盒检查逻辑，避免通过“validated 调用路径”绕过安全策略。
      - 对静态方法调用：
        - 在 `OPCODE_CALL_NATIVE_STATIC` / `OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN` / `OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN` 中，在 `method->call(nullptr, ...)` 或 `method->validated_call(nullptr, ...)` 前调用 `_gdscript_sandbox_check_method_bind(...)`，令诸如 `FileAccess.open(...)`、`DirAccess.open(...)`、`ResourceLoader.load(...)` 等静态入口同样受沙盒控制。
      - 路径安全：
        - 在 `_gdscript_sandbox_check_method_bind(...)` 内，针对 `FileAccess` / `DirAccess` / `ResourceLoader` / `ResourceSaver` 等类，将 **第一个字符串参数** 视为路径，使用 `HMSandboxConfig::is_path_allowed(...)` 做白名单与目录穿越校验：
          - 允许默认前缀 `res://` / `user://`；
          - 拒绝包含 `..` 目录穿越片段或绝对盘符（如 `C:/secret.txt`）的路径，并记录对应安全错误。
    - 影响与验证：
      - 对于通过 `ResourceFormatLoaderHMScript` 加载、已被标记为 `sandbox_enabled = true` 的 `.hm` / `.hmc` 脚本：
        - 调用 `OS.get_name()` 等被禁用类 / 方法会被沙盒拦截，并在 `"hm_default"` profile 下产生结构化错误记录。
        - 调用 `FileAccess.open("C:/secret.txt", ...)` 等访问危险路径的静态方法会因路径不在白名单而被拒绝，保障文件系统访问仅限 `res://` / `user://` 等安全范围。
      - 对于普通 `.gd` 脚本（默认未启用沙盒标记），上述改动不改变行为，仍然按原生 GDScript 语义执行。

