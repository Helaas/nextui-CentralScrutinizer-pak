"use client";

import { useEffect, useRef, useState } from "react";
import type { FitAddon } from "@xterm/addon-fit";
import type { Terminal } from "@xterm/xterm";
import "@xterm/xterm/css/xterm.css";

import { createTerminalSession } from "../lib/api";
import type { SessionResponse } from "../lib/types";

function describeTerminalError(error: string): string {
  if (error === "terminal_disabled") {
    return "Terminal access was disabled on the handheld.";
  }

  return "Terminal connection failed.";
}

export function TerminalToolView({
  enabled,
  onBack,
  refreshSession,
}: {
  enabled: boolean;
  onBack: () => void;
  refreshSession: () => Promise<SessionResponse>;
}) {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const socketRef = useRef<WebSocket | null>(null);
  const terminalRef = useRef<Terminal | null>(null);
  const fitAddonRef = useRef<FitAddon | null>(null);
  const [status, setStatus] = useState<"idle" | "connecting" | "connected">("idle");
  const [notice, setNotice] = useState<string | null>(null);

  useEffect(() => {
    return () => {
      socketRef.current?.close();
      terminalRef.current?.dispose();
      socketRef.current = null;
      terminalRef.current = null;
      fitAddonRef.current = null;
    };
  }, []);

  useEffect(() => {
    if (status === "idle" || !containerRef.current || terminalRef.current) {
      return;
    }

    let disposed = false;
    let term: Terminal | null = null;
    let fitAddon: FitAddon | null = null;
    let removeResizeListener: (() => void) | null = null;

    void Promise.all([import("@xterm/xterm"), import("@xterm/addon-fit")]).then(([xtermModule, fitModule]) => {
      if (disposed || !containerRef.current || terminalRef.current) {
        return;
      }

      term = new xtermModule.Terminal({
        allowTransparency: true,
        cursorBlink: true,
        fontFamily: '"Menlo", "SFMono-Regular", "Consolas", monospace',
        fontSize: 13,
        theme: {
          background: "#111111",
          foreground: "#f4f4f4",
        },
      });
      fitAddon = new fitModule.FitAddon();

      const handleResize = () => {
        if (!fitAddonRef.current || !terminalRef.current) {
          return;
        }

        fitAddonRef.current.fit();
        if (socketRef.current?.readyState === WebSocket.OPEN) {
          socketRef.current.send(
            JSON.stringify({
              type: "resize",
              cols: terminalRef.current.cols,
              rows: terminalRef.current.rows,
            }),
          );
        }
      };

      terminalRef.current = term;
      fitAddonRef.current = fitAddon;
      term.loadAddon(fitAddon);
      term.open(containerRef.current);
      fitAddon.fit();
      term.writeln("Central Scrutinizer terminal");
      term.writeln("");
      term.onData((data) => {
        if (socketRef.current?.readyState === WebSocket.OPEN) {
          socketRef.current.send(data);
        }
      });
      window.addEventListener("resize", handleResize);
      removeResizeListener = () => {
        window.removeEventListener("resize", handleResize);
      };

      if (socketRef.current?.readyState === WebSocket.OPEN) {
        handleResize();
      }
    });

    return () => {
      disposed = true;
      if (removeResizeListener) {
        removeResizeListener();
      }
      if (term) {
        term.dispose();
      }
      terminalRef.current = null;
      fitAddonRef.current = null;
    };
  }, [status]);

  useEffect(() => {
    if (enabled) {
      return;
    }

    socketRef.current?.close();
    socketRef.current = null;
    setStatus("idle");
  }, [enabled]);

  async function handleConnect() {
    let nextSession: SessionResponse;
    let socket: WebSocket;
    let baseUrl: URL;

    setNotice(null);
    setStatus("connecting");

    try {
      nextSession = await refreshSession();
      if (!nextSession.capabilities.terminal) {
        setNotice("Terminal access is disabled on the handheld.");
        setStatus("idle");
        return;
      }
      if (!nextSession.csrf) {
        throw new Error("Missing session csrf token.");
      }

      const { ticket } = await createTerminalSession(nextSession.csrf);
      baseUrl = new URL(window.location.href);
      socket = new WebSocket(
        `${baseUrl.protocol === "https:" ? "wss" : "ws"}://${baseUrl.host}/api/terminal/socket`,
      );
      socket.binaryType = "arraybuffer";
      socketRef.current = socket;

      socket.addEventListener("open", () => {
        socket.send(JSON.stringify({ type: "auth", ticket }));
      });
      socket.addEventListener("message", (event) => {
        if (typeof event.data === "string") {
          try {
            const message = JSON.parse(event.data) as { type?: string; error?: string };

            if (message.type === "ready") {
              setStatus("connected");
              if (fitAddonRef.current && terminalRef.current) {
                fitAddonRef.current.fit();
                socket.send(
                  JSON.stringify({
                    type: "resize",
                    cols: terminalRef.current.cols,
                    rows: terminalRef.current.rows,
                  }),
                );
              }
              return;
            }
            if (message.type === "error") {
              setNotice(describeTerminalError(message.error ?? ""));
              socket.close();
              return;
            }
            if (message.type === "closed") {
              setNotice("Terminal session closed.");
              socket.close();
            }
          } catch {
            // Ignore non-JSON text frames.
          }
          return;
        }

        if (event.data instanceof ArrayBuffer) {
          terminalRef.current?.write(new Uint8Array(event.data));
        }
      });
      socket.addEventListener("close", () => {
        if (socketRef.current === socket) {
          socketRef.current = null;
        }
        setStatus("idle");
      });
      socket.addEventListener("error", () => {
        setNotice("Terminal connection failed.");
        socket.close();
      });
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Terminal connection failed.");
      setStatus("idle");
    }
  }

  function handleDisconnect() {
    socketRef.current?.close();
    socketRef.current = null;
    setStatus("idle");
  }

  return (
    <div className="space-y-5">
      <button
        className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
        onClick={() => {
          handleDisconnect();
          onBack();
        }}
        type="button"
      >
        <span aria-hidden="true">←</span>
        Back
      </button>
      <div className="space-y-3">
        <h2 className="text-lg font-semibold">Terminal</h2>
        <p className="text-sm text-[var(--muted)]">
          This opens a real shell on the device. Use it only when you understand the commands you are
          running.
        </p>
      </div>
      {!enabled ? (
        <div className="rounded-2xl border border-[var(--border)] bg-[var(--panel)] px-5 py-5 text-sm text-[var(--muted)]">
          Terminal access is disabled on the handheld. Enable it from the device settings screen first.
        </div>
      ) : (
        <>
          {notice ? (
            <div className="rounded-lg border border-[var(--border)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--muted)]">
              {notice}
            </div>
          ) : null}
          {status === "idle" ? (
            <div className="rounded-2xl border border-[var(--border)] bg-[var(--panel)] px-5 py-5">
              <p className="text-sm text-[var(--muted)]">
                The session starts in the SD card root and uses the device shell through a PTY-backed websocket.
              </p>
              <button
                className="mt-4 rounded-full border border-[var(--border)] bg-[var(--accent-soft)] px-4 py-2 text-sm font-semibold text-[var(--accent)] transition hover:border-[var(--accent)]/40"
                onClick={() => {
                  void handleConnect();
                }}
                type="button"
              >
                Acknowledge And Connect
              </button>
            </div>
          ) : null}
          {(status === "connecting" || status === "connected") ? (
            <div className="space-y-3">
              <div className="flex items-center justify-between gap-3">
                <p className="text-sm text-[var(--muted)]">
                  {status === "connecting" ? "Connecting terminal..." : "Connected"}
                </p>
                <button
                  className="rounded-full border border-[var(--border)] px-4 py-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
                  onClick={handleDisconnect}
                  type="button"
                >
                  Disconnect
                </button>
              </div>
              <div className="rounded-2xl border border-[var(--border)] bg-black/40 p-3">
                <div className="min-h-[26rem]" ref={containerRef} />
              </div>
            </div>
          ) : null}
        </>
      )}
    </div>
  );
}
