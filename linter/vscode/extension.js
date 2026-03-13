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
		const candidates = [
			path.join(context.extensionPath, "homot-lsp.exe"),
			path.join(context.extensionPath, "..", "..", "bin", "linter", "homot-lsp.exe"),
			// Non-Windows:
			path.join(context.extensionPath, "homot-lsp"),
			path.join(context.extensionPath, "..", "..", "bin", "linter", "homot-lsp"),
		];
		serverPath = candidates.find((p) => {
			try { require("fs").accessSync(p); return true; } catch { return false; }
		});
		if (!serverPath) {
			window.showErrorMessage(
				"homot-lsp executable not found. Set homotLsp.serverPath in settings."
			);
			return;
		}
	}

	// Resolve linterdb.json path.
	let dbPath = config.get("dbPath");
	if (!dbPath && workspace.workspaceFolders) {
		const wsRoot = workspace.workspaceFolders[0].uri.fsPath;
		const candidate = path.join(wsRoot, "linterdb.json");
		try { require("fs").accessSync(candidate); dbPath = candidate; } catch {}
	}

	// Build server arguments.
	const args = ["--stdio"];
	if (dbPath) {
		args.push("--db", dbPath);
	}

	const serverOptions = {
		run: { command: serverPath, args, transport: TransportKind.stdio },
		debug: { command: serverPath, args, transport: TransportKind.stdio },
	};

	const clientOptions = {
		documentSelector: [
			{ scheme: "file", language: "gdscript" },
			{ scheme: "file", language: "hmscript" },
		],
		synchronize: {
			fileEvents: workspace.createFileSystemWatcher("**/*.{gd,hm,hmc}"),
		},
	};

	client = new LanguageClient(
		"homotLsp",
		"Homot GDScript LSP",
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
