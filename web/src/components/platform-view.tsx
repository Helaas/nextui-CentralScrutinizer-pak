import type { PlatformResource, PlatformSummary } from "../lib/types";
import { ResourceCardGrid } from "./resource-card-grid";

export function PlatformView({
  platform,
  onBack,
  onOpenResource,
}: {
  platform: PlatformSummary;
  onBack: () => void;
  onOpenResource: (resource: PlatformResource) => void;
}) {
  return (
    <div className="space-y-6">
      <button
        className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
        onClick={onBack}
        type="button"
      >
        <span aria-hidden="true">←</span>
        Back to Library
      </button>
      <ResourceCardGrid onSelect={onOpenResource} platform={platform} />
    </div>
  );
}
