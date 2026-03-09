/**************************************************************************/
/*  sandbox_config.cpp                                                    */
/**************************************************************************/

#include "sandbox_config.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/error/error_macros.h"

namespace hmsandbox {

HMSandboxConfig::HMSandboxConfig() {
	// NOTE: Do NOT call setup_default_blocklist() here.
	// Static dummy instances (HMSandbox::dummy_config) are constructed during
	// C++ static initialization, before StringName::setup() is called.
	// block_class() creates StringName objects which would crash.
	// Default blocklist is set up lazily on first access instead.
}

void HMSandboxConfig::ensure_default_blocklist() {
	if (!default_blocklist_initialized) {
		default_blocklist_initialized = true;
		setup_default_blocklist();
	}
}

Error HMSandboxConfig::load(const String &p_path) {
	if (!FileAccess::exists(p_path)) {
		return ERR_FILE_NOT_FOUND;
	}

	Ref<FileAccess> fa = FileAccess::open(p_path, FileAccess::READ);
	if (fa.is_null()) {
		return ERR_CANT_OPEN;
	}

	const String content = fa->get_as_text();
	fa.unref();

	Ref<JSON> json;
	json.instantiate();
	const Error err = json->parse(content);
	if (err != OK) {
		ERR_PRINT(vformat("HMSandbox: Failed to parse sandbox JSON: %s", json->get_error_message()));
		return err;
	}

	const Variant data = json->get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		return ERR_INVALID_DATA;
	}

	const Dictionary dict = data;

	if (dict.has("blocked_classes")) {
		const Array arr = dict["blocked_classes"];
		for (int i = 0; i < arr.size(); i++) {
			block_class(arr[i]);
		}
	}

	if (dict.has("blocked_methods")) {
		const Array arr = dict["blocked_methods"];
		for (int i = 0; i < arr.size(); i++) {
			const String spec = arr[i];
			const int dot_pos = spec.find(".");
			if (dot_pos > 0) {
				const String class_name = spec.left(dot_pos);
				const String method_name = spec.substr(dot_pos + 1);
				block_method(class_name, method_name);
			}
		}
	}

	if (dict.has("blocked_properties")) {
		const Array arr = dict["blocked_properties"];
		for (int i = 0; i < arr.size(); i++) {
			const String spec = arr[i];
			const int dot_pos = spec.find(".");
			if (dot_pos > 0) {
				const String class_name = spec.left(dot_pos);
				const String prop_name = spec.substr(dot_pos + 1);
				block_property(class_name, prop_name);
			}
		}
	}

	if (dict.has("allowed_paths")) {
		const Array arr = dict["allowed_paths"];
		for (int i = 0; i < arr.size(); i++) {
			add_allowed_path_prefix(arr[i]);
		}
	}

	return OK;
}

void HMSandboxConfig::block_class(const StringName &p_class_name) {
	blocked_classes.insert(p_class_name);
}

bool HMSandboxConfig::is_class_or_parent_blocked(const StringName &p_class_name) const {
	const_cast<HMSandboxConfig *>(this)->ensure_default_blocklist();
	StringName current = p_class_name;
	while (!current.is_empty()) {
		if (blocked_classes.has(current)) {
			return true;
		}
		current = ClassDB::get_parent_class(current);
	}
	return false;
}

void HMSandboxConfig::block_method(const StringName &p_class_name, const StringName &p_method_name) {
	blocked_methods.insert(String(p_class_name) + "." + String(p_method_name));
}

bool HMSandboxConfig::is_method_blocked_with_inheritance(const StringName &p_class_name, const StringName &p_method_name) const {
	const_cast<HMSandboxConfig *>(this)->ensure_default_blocklist();
	StringName current = p_class_name;
	while (!current.is_empty()) {
		const String key = String(current) + "." + String(p_method_name);
		if (blocked_methods.has(key)) {
			return true;
		}
		current = ClassDB::get_parent_class(current);
	}
	return false;
}

void HMSandboxConfig::block_property(const StringName &p_class_name, const StringName &p_property_name) {
	blocked_properties.insert(String(p_class_name) + "." + String(p_property_name));
}

bool HMSandboxConfig::is_property_blocked_with_inheritance(const StringName &p_class_name, const StringName &p_property_name) const {
	const_cast<HMSandboxConfig *>(this)->ensure_default_blocklist();
	StringName current = p_class_name;
	while (!current.is_empty()) {
		const String key = String(current) + "." + String(p_property_name);
		if (blocked_properties.has(key)) {
			return true;
		}
		current = ClassDB::get_parent_class(current);
	}
	return false;
}

void HMSandboxConfig::add_allowed_path_prefix(const String &p_prefix) {
	allowed_path_prefixes.insert(p_prefix);
}

bool HMSandboxConfig::is_path_allowed(const String &p_path) const {
	// 拒绝常见目录穿越模式。
	if (p_path.find("/../") != -1 ||
			p_path.ends_with("/..") ||
			p_path.begins_with("../") ||
			p_path == ".." ||
			p_path.find("\\..") != -1 ||
			p_path.find("..\\") != -1) {
		return false;
	}

	// 必须以允许的前缀开头。
	for (const String &prefix : allowed_path_prefixes) {
		if (p_path.begins_with(prefix)) {
			return true;
		}
	}

	return false;
}

void HMSandboxConfig::reset() {
	blocked_classes.clear();
	blocked_methods.clear();
	blocked_properties.clear();
	allowed_path_prefixes.clear();
	default_blocklist_initialized = false;
	ensure_default_blocklist();
}

void HMSandboxConfig::setup_default_blocklist() {
	// 文件系统。
	block_class("FileAccess");
	block_class("DirAccess");

	// OS / 进程。
	block_class("OS");

	// 网络。
	block_class("HTTPClient");
	block_class("HTTPRequest");
	block_class("StreamPeer");
	block_class("StreamPeerTCP");
	block_class("StreamPeerTLS");
	block_class("TCPServer");
	block_class("UDPServer");
	block_class("PacketPeer");
	block_class("PacketPeerUDP");
	block_class("PacketPeerStream");
	block_class("WebSocketPeer");

	// 线程 / 同步。
	block_class("Thread");
	block_class("Mutex");
	block_class("Semaphore");
	block_class("WorkerThreadPool");

	// 其他执行入口。
	// GDScript 不再完全禁止，因为 HMScript 内部类实例化需要它。
	// 只禁止 C# 和原生扩展动态加载
	block_class("NativeExtension");

	// 资源加载保存底层接口。
	block_class("ResourceLoader");
	block_class("ResourceSaver");

	// 编辑器相关。
	block_class("EditorInterface");
	block_class("EditorPlugin");
	block_class("EditorScript");
	block_class("ProjectSettings");

	// 反射 / 动态调用。
	block_method("Object", "call");
	block_method("Object", "callv");
	block_method("Object", "set");
	block_method("Object", "set_deferred");
	block_method("Object", "call_deferred");
	block_method("Object", "free");

	block_method("ClassDB", "instantiate");
	block_method("ClassDB", "instance");
	block_method("ClassDB", "get_class_list");

	block_method("Engine", "get_singleton");
	block_method("Engine", "register_singleton");
	block_method("Engine", "unregister_singleton");

	// 默认允许路径前缀。
	allowed_path_prefixes.insert("res://");
	allowed_path_prefixes.insert("user://");
}

} // namespace hmsandbox

