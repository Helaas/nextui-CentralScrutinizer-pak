import { useEffect, useRef } from "react";

type NoticeTone = "error" | "info" | "success";

const DISMISS_DELAY_MS: Record<NoticeTone, number> = {
  error: 8000,
  info: 6000,
  success: 5000,
};

function inferTone(message: string): NoticeTone {
  const normalized = message.toLowerCase();

  if (
    normalized.includes("can't")
    || normalized.includes("could not")
    || normalized.includes("disabled")
    || normalized.includes("failed")
    || normalized.includes("invalid")
    || normalized.includes("missing")
    || normalized.includes("no longer")
    || normalized.includes("unavailable")
  ) {
    return "error";
  }
  if (
    normalized.includes("created")
    || normalized.includes("deleted")
    || normalized.includes("downloaded")
    || normalized.includes("moved")
    || normalized.includes("renamed")
    || normalized.includes("saved")
    || normalized.includes("uploaded")
  ) {
    return "success";
  }

  return "info";
}

export function NoticeToast({
  autoDismissMs,
  message,
  onDismiss,
  tone,
}: {
  autoDismissMs?: number;
  message: string;
  onDismiss?: () => void;
  tone?: NoticeTone;
}) {
  const resolvedTone = tone ?? inferTone(message);
  const dismissDelay = autoDismissMs ?? DISMISS_DELAY_MS[resolvedTone];
  const dismissRef = useRef(onDismiss);
  const canDismiss = Boolean(onDismiss);
  const toneClass =
    resolvedTone === "error"
      ? "border-rose-300/35 bg-rose-950/95 text-rose-50"
      : resolvedTone === "success"
        ? "border-emerald-300/35 bg-emerald-950/95 text-emerald-50"
        : "border-[var(--border)] bg-[var(--panel)] text-[var(--text)]";

  useEffect(() => {
    dismissRef.current = onDismiss;
  }, [onDismiss]);

  useEffect(() => {
    if (!canDismiss || dismissDelay <= 0) {
      return;
    }

    const timer = window.setTimeout(() => {
      dismissRef.current?.();
    }, dismissDelay);

    return () => {
      window.clearTimeout(timer);
    };
  }, [canDismiss, dismissDelay, message]);

  return (
    <div
      aria-live={resolvedTone === "error" ? "assertive" : "polite"}
      className="fixed bottom-24 left-4 right-4 z-50 flex justify-center md:bottom-6 md:left-auto md:right-6 md:justify-end"
      role={resolvedTone === "error" ? "alert" : "status"}
    >
      <div className={`flex max-w-xl items-start gap-3 rounded-2xl border px-4 py-3 text-sm shadow-2xl backdrop-blur ${toneClass}`}>
        <div className="min-w-0 flex-1">
          <p className="font-semibold">
            {resolvedTone === "error" ? "Action needed" : resolvedTone === "success" ? "Done" : "Notice"}
          </p>
          <p className="mt-1 break-words">{message}</p>
        </div>
        {onDismiss ? (
          <button
            aria-label="Dismiss notice"
            className="shrink-0 rounded-md border border-white/15 px-2 py-1 text-xs font-semibold opacity-80 transition hover:opacity-100"
            onClick={onDismiss}
            type="button"
          >
            Dismiss
          </button>
        ) : null}
      </div>
    </div>
  );
}
