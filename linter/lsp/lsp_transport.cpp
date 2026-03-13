/**************************************************************************/
/*  lsp_transport.cpp                                                     */
/**************************************************************************/

#ifdef HOMOT

#include "lsp_transport.h"

#include "core/io/json.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace lsp {

// Ensure stdin/stdout are in binary mode on Windows so \r\n is not mangled.
static bool _binary_mode_set = false;
static void _ensure_binary_mode() {
	if (_binary_mode_set) {
		return;
	}
	_binary_mode_set = true;
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif
}

// Read exactly n bytes from stdin. Returns false on EOF/error.
static bool _read_exact(char *r_buf, int p_count) {
	int read_so_far = 0;
	while (read_so_far < p_count) {
		int c = fgetc(stdin);
		if (c == EOF) {
			return false;
		}
		r_buf[read_so_far++] = (char)c;
	}
	return true;
}

// Read a single header line (up to \r\n). Returns false on EOF.
static bool _read_header_line(String &r_line) {
	r_line = String();
	CharString buf;
	while (true) {
		int c = fgetc(stdin);
		if (c == EOF) {
			return false;
		}
		if (c == '\r') {
			int next = fgetc(stdin);
			if (next == '\n') {
				r_line = String::utf8(buf.get_data(), buf.length());
				return true;
			}
			// Shouldn't happen in valid LSP, but handle gracefully.
			buf += (char)c;
			if (next != EOF) {
				buf += (char)next;
			}
		} else {
			buf += (char)c;
		}
	}
}

Dictionary Transport::read_message() {
	_ensure_binary_mode();

	// Read headers until empty line.
	int content_length = -1;
	while (true) {
		String header;
		if (!_read_header_line(header)) {
			return Dictionary(); // EOF
		}
		if (header.is_empty()) {
			break; // End of headers.
		}
		if (header.begins_with("Content-Length:")) {
			content_length = header.substr(15).strip_edges().to_int();
		}
		// Ignore other headers (Content-Type, etc.)
	}

	if (content_length <= 0) {
		return Dictionary(); // Invalid or missing Content-Length.
	}

	// Read body.
	char *body = (char *)memalloc(content_length + 1);
	if (!_read_exact(body, content_length)) {
		memfree(body);
		return Dictionary();
	}
	body[content_length] = '\0';

	String json_str = String::utf8(body, content_length);
	memfree(body);

	// Parse JSON.
	Variant parsed = JSON::parse_string(json_str);
	if (parsed.get_type() == Variant::DICTIONARY) {
		return parsed;
	}
	return Dictionary();
}

void Transport::write_message(const Dictionary &p_msg) {
	_ensure_binary_mode();

	String json = JSON::stringify(p_msg, "", false);
	CharString utf8 = json.utf8();
	int len = utf8.length();

	// Write header + body.
	fprintf(stdout, "Content-Length: %d\r\n\r\n", len);
	fwrite(utf8.get_data(), 1, len, stdout);
	fflush(stdout);
}

} // namespace lsp

#endif // HOMOT
