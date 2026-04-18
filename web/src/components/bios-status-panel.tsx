import type { BiosRequirement, BiosSummary } from "../lib/types";

export function BiosStatusPanel({
  requirements,
  summary,
}: {
  requirements: BiosRequirement[];
  summary?: BiosSummary;
}) {
  return (
    <section className="rounded-[28px] border border-[var(--border)] bg-[var(--panel)] p-6">
      <div className="flex flex-col gap-4 sm:flex-row sm:items-end sm:justify-between">
        <div>
          <h2 className="text-xl font-semibold">BIOS Status</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">
            Presence-based check for the BIOS files mapped to this platform.
          </p>
        </div>
        {summary ? (
          <div className="text-sm text-[var(--muted)]">
            {summary.present}/{summary.required} present
            <span className={`ml-3 font-semibold ${summary.satisfied ? "text-emerald-300" : "text-amber-300"}`}>
              {summary.satisfied ? "Ready" : "Needs files"}
            </span>
          </div>
        ) : null}
      </div>
      <div className="mt-5 grid gap-3">
        {requirements.length === 0 ? (
          <p className="text-sm text-[var(--muted)]">No BIOS requirements are mapped for this platform.</p>
        ) : (
          requirements.map((requirement) => (
            <div
              key={`${requirement.path}-${requirement.fileName}`}
              className="flex flex-col gap-2 rounded-2xl border border-[var(--line)] bg-black/10 px-4 py-3 sm:flex-row sm:items-center sm:justify-between"
            >
              <div>
                <p className="font-semibold">{requirement.fileName}</p>
                <p className="text-sm text-[var(--muted)]">{requirement.path}</p>
              </div>
              <span
                className={`rounded-full px-3 py-1 text-xs font-semibold uppercase tracking-[0.22em] ${
                  requirement.status === "present"
                    ? "bg-emerald-500/15 text-emerald-200"
                    : "bg-amber-500/15 text-amber-200"
                }`}
              >
                {requirement.status}
              </span>
            </div>
          ))
        )}
      </div>
    </section>
  );
}
