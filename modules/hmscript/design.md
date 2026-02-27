
- **目标**：在不复制一整套 GDScript 的前提下，**直接在现有 GDScript 实现上加“强沙盒模式”**，并且尽量只影响被标记为“沙盒脚本”的调用路径（比如 HMScript 或带某种注解的脚本），普通 GDScript 尽量保持原样。
- **核心改造点**：
  - 安全配置（类/方法/属性/路径 blocklist）
  - API 统一入口（类似 SafeWrapper）
  - 执行/资源限流（类似 ExecutionLimiter）
  - 错误聚合 + 结构化报告
  - 沙盒上下文（脚本/实例绑定到某个 sandbox）

下面按“模块/文件 + 设计要点”来规划。

---

### 一、新增模块：安全与限流（modules/gdscript）

#### 1. `gdscript_sandbox_config.h/.cpp`（新）

- **职责**：和 JS 里的 `SandboxConfig` 一样，集中管理“这个脚本/沙盒允许访问什么”。
- **主要设计**：
  - 数据：
    - `HashSet<StringName> blocked_classes;`
    - `HashSet<String> blocked_methods;      // "ClassName.method"`
    - `HashSet<String> blocked_properties;   // "ClassName.property"`
    - `HashSet<String> allowed_path_prefixes;`
  - API：
    - `void block_class(const StringName &);`
    - `bool is_class_or_parent_blocked(const StringName &) const;`
    - `void block_method(const StringName &, const StringName &);`
    - `bool is_method_blocked_with_inheritance(const StringName &, const StringName &) const;`
    - `void block_property(...) / is_property_blocked_with_inheritance(...)`
    - `bool is_path_allowed(const String &path) const;`（带 `../` / `..\\` 检测）
    - `Error load(const String &json_path);`（可选，从 JSON 配置覆盖默认策略）
  - **默认策略**：
    - 按 JS 沙盒那一套：禁止 `FileAccess` / `DirAccess` / `OS` / 网络类 / 线程类 / `NativeExtension` / `GDExtensionManager` / `ProjectSettings` / `EditorInterface` 等。
    - 禁 `Object.call/callv/set/set_deferred/call_deferred/free`、`ClassDB.instantiate/get_class_list`、`Engine.get_singleton` 等。

#### 2. `gdscript_execution_limiter.h/.cpp`（新）

- **职责**：对应 JS 的 `ExecutionLimiter`，记录并强制执行时限、内存上限和 API 调用配额。
- **主要设计**：
  - 枚举：
    - `enum class ApiCategory { READ, WRITE, HEAVY };`
  - 数据：
    - `int64_t timeout_ms;`
    - `size_t memory_limit_bytes;`
    - 每帧的 `write_ops_this_frame`、`heavy_ops_this_frame` 以及历史总量。
    - `int64_t execution_start_time; bool is_executing;`
  - API：
    - `void set_timeout_ms(int64_t);`
    - `void set_memory_limit_mb(int);`
    - `void set_write_ops_per_frame(int);`
    - `void set_heavy_ops_per_frame(int);`
    - `void begin_execution(); void end_execution();`
    - `bool check_api_rate_limit(ApiCategory);`
    - `void reset_frame_counters();`
    - `bool is_timeout_exceeded() const;`
    - `void set_current_memory_usage(size_t); bool is_memory_limit_exceeded() const;`
  - 集成位置：
    - 每次从 GDScript 进入 native 调用前 `check_api_rate_limit`。  
    - `GDScriptLanguage::frame()` 或更全局的 frame 钩子里调用 `reset_frame_counters()`。

#### 3. `gdscript_safe_wrapper.h/.cpp`（新）

- **职责**：类似 JS `SafeWrapper`，**所有“GDScript→Godot API”在沙盒模式下都先通过它**。
- **主要设计**：
  - 成员：
    - `GDScriptSandboxConfig *config;`
    - `GDScriptExecutionLimiter *limiter;`
  - 对外 API（供 GDScript VM 使用）：
    - `Variant call_method(Object *target, const StringName &method, const Variant **args, int argcount, String &error);`
    - `Variant get_property(Object *target, const StringName &property, String &error);`
    - `bool set_property(Object *target, const StringName &property, const Variant &value, String &error);`
  - 内部逻辑：
    - 确定当前对象 class：`ClassDB::get_class_name(target)`，走 `config` 检查类/方法/属性是否被 block。  
    - 根据操作类别（READ / WRITE / HEAVY）调用 `limiter->check_api_rate_limit`。  
      - 比如：纯 getter 归为 READ；`set_*` / 添加子节点为 WRITE；`instance()`、`queue_free()`、`PackedScene.instantiate()` 为 HEAVY。  
    - 对涉及路径参数的调用（如 `FileAccess.open`、`ResourceLoader.load`）：在进入真实 `MethodBind::call` 前，用 `config->is_path_allowed()` 检查。

---

### 二、修改模块：GDScript 自身（modules/gdscript）

#### 4. `GDScript` / `GDScriptInstance`（在 `gdscript.h/.cpp` 中）

- **新增：沙盒 profile / 上下文指针**
  - 在 `GDScript` 中：
    - `bool sandbox_enabled = false;`
    - （可选）`String sandbox_id;` 或 `int sandbox_slot;` 用于把脚本挂到一个上层的“沙盒/会话管理器”。
  - 在 `GDScriptInstance` 中：
    - `bool sandbox_enabled;`
    - 指向 `GDScriptSandboxConfig` / `ExecutionLimiter` / `SafeWrapper` 或其拥有者（比如 `GDScriptSandboxContext *ctx;`）。
    - 在 `_create_instance` 时，从 `GDScript` 拷贝这些信息。

- **修改：调用路径 hook**
  - `GDScriptInstance::callp`：
    - 当前逻辑直接走 `script->member_functions[...]` → `GDScriptFunction::call`。  
    - 修改为：
      - 进入脚本函数前，`limiter->begin_execution()`；退出后 `limiter->end_execution()`。  
      - 函数体内部再发起 native 调用时，走 SafeWrapper（见下一条）。
  - GDScript VM 调用 native 的路径（在 `GDScriptFunction` 执行指令的代码里）：
    - 找到类似 “CALL” 指令执行时的 `Variant::call` 或 `Object::call`部分。  
    - 当 `instance->sandbox_enabled` 为 true 时，**不直接 `MethodBind::call`，改为通过 `GDScriptSafeWrapper`**：
      - 例：`safe_wrapper->call_method(target_obj, method_name, args, argcount, error_string);`
  - 属性访问：
    - 在处理 `GET_PROPERTY` / `SET_PROPERTY` 指令时，如果在沙盒脚本中，同样通过 `safe_wrapper` 来做检查。

- **可选：语法级约束**
  - 在 `GDScriptParser` / `Analyzer` 中检测：
    - 使用禁止类/方法时报编译期 error/warning（比纯运行时好调试）。  
    - 比如脚本里出现 `OS`、`FileAccess`、`TCPServer` 等直接静态引用时给出阻止或警告。  
  - 但这部分可以是第二阶段，先保证运行时 check 完整。

#### 5. `GDScriptLanguage`（在 `gdscript.h/.cpp`）

- **新增：沙盒管理数据**

  ```cpp
  struct SandboxProfile {
      GDScriptSandboxConfig config;
      GDScriptExecutionLimiter limiter;
      GDScriptSafeWrapper safe_wrapper;
      // 错误列表、统计信息等
  };

  HashMap<String, SandboxProfile> sandbox_profiles; // key 可用为场景名、会话 ID 等
  ```

- **API 设计**：
  - `SandboxProfile *get_sandbox_profile(const String &id);`
  - `SandboxProfile *ensure_sandbox_profile(const String &id);`
  - `void reset_sandbox_profiles_per_frame();`（在 `frame()` 被调用）
  - `Dictionary get_sandbox_errors(const String &id);`  
    `String get_sandbox_error_report(const String &id);`

- **和 HMScript 结合**（推荐用法）：
  - 在 `HMScriptLanguage::init` 或资源加载路径中：  
    - 对 `.hm` / `.hmc` 加载的 `GDScript` 调用一个新函数：`GDScript::set_sandbox_enabled(true, "hm_default")`。  
    - 这样普通 `.gd` 仍然是非沙盒，`.hm` 会走沙盒路径。
- **每帧逻辑**：
  - 在 `GDScriptLanguage::frame()` 中，对所有 `sandbox_profiles` 调 `limiter.reset_frame_counters()`。  
  - 如需要处理 Promise/协程之类，也可在这里做额外的错误 flush 或统计。

---

### 三、新增模块：错误聚合与报告（modules/gdscript）

#### 6. `gdscript_sandbox_error.h/.cpp`（新）

- **职责**：类似 JS `ErrorEntry` + `get_error_report`。
- **设计**：
  - `struct GDSandboxErrorEntry { String id, type, severity, message, file; int line, column; String stack_trace; String phase; String trigger_context; int64_t timestamp, last_occurrence; int occurrence_count; };`
  - `HashMap<String, GDSandboxErrorEntry> error_map; Vector<String> error_order;`
  - 方法：
    - `void add_error(...);`（去重 + 计数）
    - `Array get_all_errors() const;`
    - `String get_error_report_markdown() const;`

- **集成位置**：
  - 在 `GDScriptSafeWrapper` 中，当发现安全违规（blocklist/限流/路径非法）时，通过所属 `SandboxProfile` 的 `error_registry.add_error(...)` 记录。  
  - 在 `GDScriptLanguage` 与 `EngineDebugger` / `ScriptDebugger` 或 Logger 的交点处，也把 VM 错误塞进对应沙盒的 `error_registry`。

#### 7. `gdscript_sandbox_logger.h/.cpp`（新）

- **职责**：类似 JS `SandboxLogger`，挂到引擎 `Logger` 上，捕获 engine 层错误，路由到沙盒错误表。
- **设计**：
  - 继承 `Logger`，覆写 `_log_error` / `_log_message`，内部用一个线程安全队列，延迟到主线程 flush。  
  - `void set_language(GDScriptLanguage *);` 或直接访问单例。  
  - 在 flush 时，根据当前调用栈 / 脚本关联信息，推断应该归到哪个沙盒 profile（不行的话，至少归到“全局 GDScript 沙盒”）。
- **集成**：
  - 在 `GDScriptLanguage::init()` 中 `OS::get_singleton()->add_logger(sandbox_logger);`  
  - 在 `finish()` 中移除。

---

### 四、可选模块：场景/资源管线（安全加载存储）

#### 8. `gdscript_sandbox_scene.h/.cpp`（新，可选但很有用）

- **职责**：为沙盒脚本提供安全版本的场景加载/保存工具（类似 JS 的 `SceneSaver` / `SceneLoader`）。
- **设计**：
  - 对 `.tscn` 加载前做文本级检查：
    - 是否包含被禁止脚本类型（如非 HMScript 的 GDScript、C#、NativeScript 等）。  
    - 资源路径是否都在 `SandboxConfig::allowed_path_prefixes` 内。  
    - 引用的 `.hm` 是否存在。
  - 提供 API：
    - `Error sandbox_save_scene(Node *root, const String &directory, const SandboxProfile &profile);`
    - `Node *sandbox_load_scene(const String &directory, SandboxProfile &profile);`
  - 与 `HMScriptLanguage` 或上层“会话管理器”联动，用于 HM 关卡的保存/恢复。

---

### 五、整体调用流程示意（沙盒脚本）

1. `.hm` 通过 `HMScriptLanguage` 被加载，底层仍然变成一个 `GDScript`。  
2. 在 `HMScriptLanguage` 的加载/注册路径中，对该 `GDScript` 调用 `set_sandbox_enabled(true, sandbox_id)`。  
3. `GDScriptInstance::_create_instance` 根据脚本上的标记，给实例挂上对应的 `SandboxProfile`（其中含 `SandboxConfig` / `ExecutionLimiter` / `SafeWrapper` / `ErrorRegistry`）。  
4. 脚本运行时，每次从字节码发起的 Object 调用 / 属性访问，检查到是沙盒实例 → 通过 `SafeWrapper`：
   - `SandboxConfig` 判安全策略；
   - `ExecutionLimiter` 判配额/时限；
   - 若违规，记入 `ErrorRegistry` 并抛出合适的错误（可由 ScriptDebugger 捕获）。
5. 每帧 `GDScriptLanguage::frame` 重置限流计数，同时可以触发 batched error 更新信号，供编辑器或外部逻辑查看。

---

### 总结

- **添加的新模块**：  
  - `gdscript_sandbox_config.*`（策略）  
  - `gdscript_execution_limiter.*`（限流）  
  - `gdscript_safe_wrapper.*`（统一入口）  
  - `gdscript_sandbox_error.*`（错误聚合）  
  - `gdscript_sandbox_logger.*`（错误收集）  
  - （可选）`gdscript_sandbox_scene.*`（安全场景管线）
- **修改的现有模块**：  
  - `gdscript.h/.cpp` 中的 `GDScript`、`GDScriptInstance`、`GDScriptLanguage`：  
    - 加沙盒标记/上下文、在 VM 调用路径挂 `SafeWrapper`、在 `frame()` 里重置限流、提供错误/报告查询接口。  
- 这样可以在**保留现有 GDScript 编辑器体验**的前提下，为特定脚本（如 HMScript）提供“接近 JS 沙盒”的安全模型。