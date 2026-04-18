import { useRef, useState } from "react";

export function PairScreen({
  message,
  onSubmit,
  isBusy = false,
  error,
}: {
  message?: string | null;
  onSubmit: (code: string) => Promise<void>;
  isBusy?: boolean;
  error?: string | null;
}) {
  const [code, setCode] = useState("");
  const inputRef = useRef<HTMLInputElement | null>(null);
  const digits = Array.from({ length: 4 }, (_, index) => code[index] ?? "");

  return (
    <main className="flex min-h-screen items-center justify-center px-4 py-10 text-[var(--text)]">
      <section className="w-full max-w-sm space-y-6 rounded-xl border border-[var(--border)] bg-[var(--panel)] p-8 shadow-[var(--shadow)]">
        <div className="space-y-3 text-center">
          <img alt="The Central Scrutinizer" className="mx-auto h-32 w-auto" src="/logo.png" />
          <h1 className="text-2xl font-bold tracking-tight">The Central Scrutinizer</h1>
          <p className="text-sm font-semibold text-[var(--muted)]">Please enter the PIN shown on the device.</p>
        </div>
        <form
          className="space-y-5"
          onSubmit={(event) => {
            event.preventDefault();
            if (code.length === 4 && !isBusy) {
              void onSubmit(code);
            }
          }}
        >
          <label className="sr-only" htmlFor="pair-code">
            Pairing code
          </label>
          <div
            className="relative"
            onClick={() => {
              inputRef.current?.focus();
            }}
          >
            <input
              ref={inputRef}
              aria-label="Pairing code"
              autoFocus
              className="absolute inset-0 opacity-0"
              id="pair-code"
              inputMode="numeric"
              maxLength={4}
              onChange={(event) => {
                setCode(event.target.value.replace(/\D/g, "").slice(0, 4));
              }}
              value={code}
            />
            <div className="flex justify-center gap-2">
              {digits.map((digit, index) => {
                const isActive = index === Math.min(code.length, 3);
                const borderColor = error
                  ? "border-rose-400"
                  : digit || isActive
                    ? "border-[var(--accent)]"
                    : "border-[var(--border)]";
                return (
                  <div
                    key={index}
                    className={`flex h-14 w-14 items-center justify-center border bg-[var(--bg)] text-center font-mono text-xl font-bold text-[var(--text)] transition-colors ${borderColor}`}
                  >
                    {digit || ""}
                  </div>
                );
              })}
            </div>
          </div>
          {message ? <p className="text-center text-sm text-[var(--muted)]">{message}</p> : null}
          {error ? <p className="text-center text-sm font-medium text-rose-300">{error}</p> : null}
          <button
            className="w-full rounded-md bg-[var(--accent)] px-4 py-3 text-sm font-semibold uppercase tracking-wider text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50"
            disabled={code.length !== 4 || isBusy}
            type="submit"
          >
            {isBusy ? "Pairing..." : "Pair Browser"}
          </button>
        </form>
      </section>
    </main>
  );
}
