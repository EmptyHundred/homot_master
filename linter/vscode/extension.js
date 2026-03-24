const path = require("path");
const { workspace, window } = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

function activate(context) {
	const config = workspace.getConfiguration("homotLsp");

	// Resolve server executable path.
	let serverPath = config.get("serverPath");
	if (!serverPath) {
		// Default: look next to the extension, then in the repo bin/.
		// The binary is named "homot" (with possible platform/target suffixes).
		const candidates = [
			path.join(context.extensionPath, "homot.exe"),
			path.join(context.extensionPath, "..", "..", "bin", "linter", "homot.exe"),
			// Non-Windows:
			path.join(context.extensionPath, "homot"),
			path.join(context.extensionPath, "..", "..", "bin", "linter", "homot"),
		];
		serverPath = candidates.find((p) => {
			try { require("fs").accessSync(p); return true; } catch { return false; }
		});

		// Also try glob-matching for versioned binaries (e.g. homot.windows.template_debug.x86_64.exe).
		if (!serverPath) {
			const fs = require("fs");
			const dirs = [
				context.extensionPath,
				path.join(context.extensionPath, "..", "..", "bin", "linter"),
			];
			for (const dir of dirs) {
				try {
					const files = fs.readdirSync(dir);
					const match = files.find((f) => f.startsWith("homot.") && (f.endsWith(".exe") || !f.includes(".")));
					if (match) {
						serverPath = path.join(dir, match);
						break;
					}
				} catch {}
			}
		}

		if (!serverPath) {
			window.showErrorMessage(
				"homot executable not found. Set homotLsp.serverPath in settings."
			);
			return;
		}
	}

	// Build server arguments: "serve" subcommand.
	const args = ["serve"];

	// Optional: override embedded linterdb with external JSON.
	const dbPath = config.get("dbPath");
	if (dbPath) {
		// --db must come before the subcommand.
		args.unshift("--db", dbPath);
	}

	const serverOptions = {
		run: { command: serverPath, args, transport: TransportKind.stdio },
		debug: { command: serverPath, args, transport: TransportKind.stdio },
	};

	const clientOptions = {
		documentSelector: [
			{ scheme: "file", language: "gdscript" },
			{ scheme: "file", language: "hmscript" },
			{ scheme: "file", language: "gdresource" },
			{ scheme: "file", language: "gdshader" },
		],
		synchronize: {
			fileEvents: workspace.createFileSystemWatcher("**/*.{gd,hm,hmc,tscn,tres,gdshader}"),
		},
	};

	client = new LanguageClient(
		"homotLsp",
		"Homot HMScript/GDScript",
		serverOptions,
		clientOptions
	);

	client.start();
}

function deactivate() {
	if (client) {
		return client.stop();
	}
}

module.exports = { activate, deactivate };
