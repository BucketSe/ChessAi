"""ChessAi desktop app: the analysis board in a native window (Edge WebView2 on Windows)."""
import threading

import webview

import server


def main():
    httpd = server.make_server(port=0)  # free port, localhost only
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{httpd.server_address[1]}"

    webview.create_window(
        "ChessAi", url, width=1380, height=920, min_size=(900, 680), background_color="#07050d",
    )
    try:
        webview.start()
    finally:
        httpd.shutdown()
        server.close_engines()


if __name__ == "__main__":
    main()
