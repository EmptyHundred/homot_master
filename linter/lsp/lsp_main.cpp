/**************************************************************************/
/*  lsp_main.cpp                                                          */
/**************************************************************************/
/*  Standalone GDScript LSP server entry point.                           */
/*  Performs minimal engine bootstrap, then enters the LSP message loop.  */
/**************************************************************************/

#ifdef HOMOT

#include "lsp_server.h"
#include "lsp_transport.h"

#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_utility_functions.h"
#include "modules/holymolly/register_types.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/register_core_types.h"
#include "core/string/translation_server.h"
#include "servers/text/text_server.h"
#include "servers/text/text_server_dummy.h"

#ifdef _WIN32
#include "drivers/windows/dir_access_windows.h"
#include "drivers/windows/file_access_windows.h"
#endif

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Suppress stderr during static initialization (same as main_linter.cpp).
// ---------------------------------------------------------------------------

static int _lsp_saved_stderr_fd = -1;

#ifdef _WIN32
#pragma init_seg(compiler)
static struct _LspStderrGuard {
	_LspStderrGuard() {
		fflush(stderr);
		_lsp_saved_stderr_fd = _dup(_fileno(stderr));
		FILE *nul = fopen("NUL", "w");
		if (nul) {
			_dup2(_fileno(nul), _fileno(stderr));
			fclose(nul);
		}
	}
} _lsp_stderr_guard;
#else
__attribute__((constructor(101)))
static void _lsp_suppress_stderr() {
	fflush(stderr);
	_lsp_saved_stderr_fd = dup(fileno(stderr));
	FILE *nul = fopen("/dev/null", "w");
	if (nul) {
		dup2(fileno(nul), fileno(stderr));
		fclose(nul);
	}
}
#endif

static void _lsp_restore_stderr() {
	if (_lsp_saved_stderr_fd >= 0) {
		fflush(stderr);
#ifdef _WIN32
		_dup2(_lsp_saved_stderr_fd, _fileno(stderr));
		_close(_lsp_saved_stderr_fd);
#else
		dup2(_lsp_saved_stderr_fd, fileno(stderr));
		close(_lsp_saved_stderr_fd);
#endif
		_lsp_saved_stderr_fd = -1;
	}
}

// ---------------------------------------------------------------------------
// Minimal OS (same as linter)
// ---------------------------------------------------------------------------

class OS_LSP : public OS {
protected:
	void initialize() override {}
	void initialize_joypads() override {}
	void set_main_loop(MainLoop *p_main_loop) override {}
	void delete_main_loop() override {}
	void finalize() override {}
	void finalize_core() override {}
	bool _check_internal_feature_support(const String &p_feature) override { return false; }

public:
	Vector<String> get_video_adapter_driver_info() const override { return Vector<String>(); }
	String get_stdin_string(int64_t p_buffer_size = 1024) override { return String(); }
	PackedByteArray get_stdin_buffer(int64_t p_buffer_size = 1024) override { return PackedByteArray(); }
	Error get_entropy(uint8_t *r_buffer, int p_bytes) override { return ERR_UNAVAILABLE; }
	Error execute(const String &p_path, const List<String> &p_arguments, String *r_pipe = nullptr, int *r_exitcode = nullptr, bool read_stderr = false, Mutex *p_pipe_mutex = nullptr, bool p_open_console = false) override { return ERR_UNAVAILABLE; }
	Error create_process(const String &p_path, const List<String> &p_arguments, ProcessID *r_child_id = nullptr, bool p_open_console = false) override { return ERR_UNAVAILABLE; }
	Error kill(const ProcessID &p_pid) override { return ERR_UNAVAILABLE; }
	bool is_process_running(const ProcessID &p_pid) const override { return false; }
	int get_process_exit_code(const ProcessID &p_pid) const override { return -1; }
	bool has_environment(const String &p_var) const override { return false; }
	String get_environment(const String &p_var) const override { return String(); }
	void set_environment(const String &p_var, const String &p_value) const override {}
	void unset_environment(const String &p_var) const override {}
	String get_name() const override { return "LSP"; }
	String get_distribution_name() const override { return ""; }
	String get_version() const override { return ""; }
	MainLoop *get_main_loop() const override { return nullptr; }
	DateTime get_datetime(bool utc = false) const override { return DateTime(); }
	TimeZoneInfo get_time_zone_info() const override { return TimeZoneInfo(); }
	void delay_usec(uint32_t p_usec) const override {}
	uint64_t get_ticks_usec() const override { return 0; }
};

// ---------------------------------------------------------------------------
// Platform file/dir access
// ---------------------------------------------------------------------------

static void register_platform_file_access() {
#ifdef _WIN32
	FileAccess::make_default<FileAccessWindows>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessWindows>(FileAccess::ACCESS_USERDATA);
	FileAccess::make_default<FileAccessWindows>(FileAccess::ACCESS_FILESYSTEM);
	DirAccess::make_default<DirAccessWindows>(DirAccess::ACCESS_RESOURCES);
	DirAccess::make_default<DirAccessWindows>(DirAccess::ACCESS_USERDATA);
	DirAccess::make_default<DirAccessWindows>(DirAccess::ACCESS_FILESYSTEM);
#endif
}

// ---------------------------------------------------------------------------
// GDScript module init/deinit (same as linter)
// ---------------------------------------------------------------------------

static GDScriptLanguage *gd_language = nullptr;
static Ref<ResourceFormatLoaderGDScript> gd_loader;
static Ref<ResourceFormatSaverGDScript> gd_saver;

static void init_gdscript_module() {
	GDREGISTER_CLASS(GDScript);
	gd_language = memnew(GDScriptLanguage);
	ScriptServer::register_language(gd_language);
	gd_loader.instantiate();
	ResourceLoader::add_resource_format_loader(gd_loader);
	gd_saver.instantiate();
	ResourceSaver::add_resource_format_saver(gd_saver);
	GDScriptUtilityFunctions::register_functions();
}

static void deinit_gdscript_module() {
	GDScriptUtilityFunctions::unregister_functions();
	ResourceLoader::remove_resource_format_loader(gd_loader);
	gd_loader.unref();
	ResourceSaver::remove_resource_format_saver(gd_saver);
	gd_saver.unref();
	ScriptServer::unregister_language(gd_language);
	memdelete(gd_language);
	gd_language = nullptr;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
	// Parse CLI args before engine bootstrap.
	const char *db_path_cstr = nullptr;
	bool stdio_mode = true; // Only stdio for now.

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
			db_path_cstr = argv[++i];
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			_lsp_restore_stderr();
			printf("Usage: homot-lsp [options]\n\n");
			printf("GDScript Language Server (LSP) over stdio.\n\n");
			printf("Options:\n");
			printf("  --db <path>    Path to linterdb.json\n");
			printf("  --help, -h     Show this help message\n");
			return 0;
		} else if (strcmp(argv[i], "--stdio") == 0) {
			stdio_mode = true; // Explicit, but already default.
		}
	}

	// Engine bootstrap.
	OS_LSP os_lsp;
	set_current_thread_safe_for_nodes(true);

	Engine *engine = memnew(Engine);
	register_core_types();
	register_platform_file_access();

	String db_path = db_path_cstr ? String::utf8(db_path_cstr) : String();

	ProjectSettings *globals = memnew(ProjectSettings);
	register_core_settings();

	TranslationServer *translation_server = memnew(TranslationServer);

	TextServerManager *tsman = memnew(TextServerManager);
	{
		Ref<TextServerDummy> ts;
		ts.instantiate();
		tsman->add_interface(ts);
		tsman->set_primary_interface(ts);
	}

	register_early_core_singletons();
	register_core_singletons();

	init_gdscript_module();
	GDScriptCache *gdscript_cache = memnew(GDScriptCache);

	initialize_holymolly_module(MODULE_INITIALIZATION_LEVEL_SERVERS);

	// Restore stderr after bootstrap (LSP uses stderr for logging).
	_lsp_restore_stderr();

	// Create and run the LSP server.
	lsp::Server server;
	server.set_db_path(db_path);

	// Message loop — read JSON-RPC messages from stdin, dispatch, repeat.
	while (true) {
		Dictionary msg = lsp::Transport::read_message();
		if (msg.is_empty()) {
			break; // EOF — client disconnected.
		}
		if (!server.process_message(msg)) {
			break; // exit notification received.
		}
	}

	// Cleanup.
	uninitialize_holymolly_module(MODULE_INITIALIZATION_LEVEL_SERVERS);
	GDScriptCache::clear();
	memdelete(gdscript_cache);
	deinit_gdscript_module();

	memdelete(tsman);
	memdelete(translation_server);
	memdelete(globals);
	memdelete(engine);

	unregister_core_types();

	return 0;
}

#endif // HOMOT
