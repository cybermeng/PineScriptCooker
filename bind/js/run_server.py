import http.server
import socketserver
import os

PORT = 8000

class WasmHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        # Python 3.7+ has this built-in, but older versions might not.
        # This ensures it's always set correctly.
        self.extensions_map['.wasm'] = 'application/wasm'
        super().__init__(*args, **kwargs)

# 确保我们从正确的目录提供服务
# 这使得脚本可以从任何地方运行
web_dir = os.path.join(os.path.dirname(__file__))
os.chdir(web_dir)

with socketserver.TCPServer(("", PORT), WasmHandler) as httpd:
    print(f"Serving at http://localhost:{PORT}")
    print("Open this URL in your browser.")
    print("Press Ctrl+C to stop the server.")
    httpd.serve_forever()