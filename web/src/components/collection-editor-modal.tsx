import { useEffect, useState } from "react";

import { getBrowserAll, getPlatforms, writeTextFile } from "../lib/api";

type AvailableRom = {
  label: string;
  path: string;
  platform: string;
};

type CollectionItem = {
  fileName: string;
  name: string;
  romPaths: string[];
};

async function collectPlatformRoms(platform: {
  counts: { roms: number };
  name: string;
  romPath: string;
  tag: string;
}, csrf: string): Promise<AvailableRom[]> {
  const items: AvailableRom[] = [];
  const pendingPaths: Array<string | undefined> = [undefined];
  const visitedPaths = new Set<string>();

  while (pendingPaths.length > 0) {
    const nextPath = pendingPaths.pop();
    const browser = await getBrowserAll("roms", csrf, platform.tag, nextPath);

    for (const entry of browser.entries) {
      if (entry.type === "directory") {
        if (entry.name !== ".media" && !visitedPaths.has(entry.path)) {
          visitedPaths.add(entry.path);
          pendingPaths.push(entry.path);
        }
        continue;
      }
      if (entry.type !== "rom") {
        continue;
      }

      items.push({
        label: entry.name,
        path: `/${platform.romPath}/${entry.path}`,
        platform: platform.name,
      });
    }
  }

  return items;
}

function extractLeaf(path: string): string {
  const parts = path.split("/");

  return parts[parts.length - 1] ?? path;
}

function extractSystem(path: string): string {
  const parts = path.split("/");

  return parts.length >= 3 ? parts[2] : "";
}

export function CollectionEditorModal({
  collection,
  csrf,
  onClose,
  onSaved,
}: {
  collection: CollectionItem;
  csrf: string | null;
  onClose: () => void;
  onSaved: (paths: string[]) => void;
}) {
  const [editorPaths, setEditorPaths] = useState<string[]>(collection.romPaths);
  const [originalPaths, setOriginalPaths] = useState<string[]>(collection.romPaths);
  const [availableRoms, setAvailableRoms] = useState<AvailableRom[]>([]);
  const [validPaths, setValidPaths] = useState<Set<string>>(new Set());
  const [pickerOpen, setPickerOpen] = useState(false);
  const [pickerSearch, setPickerSearch] = useState("");
  const [selectedPaths, setSelectedPaths] = useState<string[]>([]);
  const [loadingOptions, setLoadingOptions] = useState(true);
  const [saving, setSaving] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);

  const dirty =
    editorPaths.length !== originalPaths.length ||
    editorPaths.some((path, index) => path !== originalPaths[index]);

  useEffect(() => {
    let active = true;

    async function loadOptions() {
      setLoadingOptions(true);
      setNotice(null);

      try {
        if (!csrf) {
          throw new Error("Missing session csrf token.");
        }

        const platformsResponse = await getPlatforms(csrf);
        const items: AvailableRom[] = [];

        for (const group of platformsResponse.groups) {
          for (const platform of group.platforms) {
            if (platform.counts.roms === 0) {
              continue;
            }

            items.push(...(await collectPlatformRoms(platform, csrf)));
          }
        }

        items.sort((left, right) =>
          left.platform.localeCompare(right.platform) ||
          left.label.localeCompare(right.label) ||
          left.path.localeCompare(right.path),
        );

        if (active) {
          setAvailableRoms(items);
          setValidPaths(new Set(items.map((item) => item.path)));
        }
      } catch (error) {
        if (active) {
          setNotice(error instanceof Error ? error.message : "Could not load ROM list.");
        }
      } finally {
        if (active) {
          setLoadingOptions(false);
        }
      }
    }

    void loadOptions();
    return () => {
      active = false;
    };
  }, [csrf]);

  const filteredOptions = availableRoms.filter((item) => {
    const query = pickerSearch.trim().toLowerCase();

    if (!query) {
      return true;
    }

    return item.label.toLowerCase().includes(query) || item.platform.toLowerCase().includes(query);
  });

  function move(index: number, direction: -1 | 1) {
    const nextIndex = index + direction;

    if (nextIndex < 0 || nextIndex >= editorPaths.length) {
      return;
    }

    const next = [...editorPaths];
    const swap = next[nextIndex];

    next[nextIndex] = next[index];
    next[index] = swap;
    setEditorPaths(next);
  }

  function toggleSelection(path: string) {
    setSelectedPaths((current) =>
      current.includes(path) ? current.filter((value) => value !== path) : [...current, path],
    );
  }

  function addSelectedRoms() {
    if (selectedPaths.length === 0) {
      return;
    }

    setEditorPaths((current) => {
      const existing = new Set(current);
      const additions = selectedPaths.filter((path) => !existing.has(path));

      return additions.length > 0 ? [...current, ...additions] : current;
    });
    setSelectedPaths([]);
    setPickerSearch("");
    setPickerOpen(false);
  }

  async function saveCollection() {
    if (!csrf) {
      return;
    }

    setSaving(true);
    setNotice(null);
    try {
      const content = editorPaths.join("\n") + (editorPaths.length > 0 ? "\n" : "");

      await writeTextFile({ scope: "files", path: `Collections/${collection.fileName}`, content }, csrf);
      setOriginalPaths(editorPaths);
      onSaved(editorPaths);
      setNotice("Collection saved.");
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Save failed.");
    } finally {
      setSaving(false);
    }
  }

  return (
    <div aria-labelledby="collection-editor-title" aria-modal="true" className="fixed inset-0 z-50 bg-black/60 p-4" role="dialog">
      <div className="mx-auto flex h-full max-h-[88vh] w-full max-w-5xl flex-col overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)] shadow-xl">
        <div className="flex items-center justify-between gap-4 border-b border-[var(--line)] px-5 py-4">
          <div>
            <h2 className="text-lg font-semibold" id="collection-editor-title">
              {collection.name}
            </h2>
            <p className="text-xs text-[var(--muted)]">{editorPaths.length} ROMs in collection</p>
          </div>
          <div className="flex items-center gap-2">
            <button
              className="rounded-md border border-[var(--border)] px-3 py-2 text-sm"
              onClick={() => {
                setPickerOpen(true);
              }}
              type="button"
            >
              Add ROMs
            </button>
            <button
              className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-sm font-semibold text-white disabled:cursor-not-allowed disabled:opacity-50"
              disabled={!dirty || saving || !csrf}
              onClick={() => {
                void saveCollection();
              }}
              type="button"
            >
              {saving ? "Saving..." : "Save"}
            </button>
            <button className="rounded-md px-2 py-1 text-sm text-[var(--muted)]" onClick={onClose} type="button">
              ✕
            </button>
          </div>
        </div>

        {notice ? <div className="border-b border-[var(--line)] px-5 py-3 text-sm text-[var(--muted)]">{notice}</div> : null}

        <div className="flex-1 overflow-auto px-5 py-4">
          {editorPaths.length === 0 ? (
            <div className="rounded-xl border border-[var(--border)] px-6 py-12 text-center text-sm text-[var(--muted)]">
              No ROMs in this collection.
            </div>
          ) : (
            <table className="w-full border-collapse overflow-hidden rounded-xl border border-[var(--border)] text-sm">
              <thead className="bg-white/[0.03] text-left text-xs uppercase tracking-wide text-[var(--muted)]">
                <tr>
                  <th className="px-3 py-3">#</th>
                  <th className="px-3 py-3">ROM</th>
                  <th className="px-3 py-3">System</th>
                  <th className="px-3 py-3 text-right">Actions</th>
                </tr>
              </thead>
              <tbody>
                {editorPaths.map((path, index) => {
                  const valid = validPaths.has(path);

                  return (
                    <tr key={`${path}-${index}`} className="border-t border-[var(--line)]">
                      <td className="px-3 py-3 text-[var(--muted)]">{index + 1}</td>
                      <td className="px-3 py-3">
                        <div className="flex items-center gap-2">
                          <span className={valid ? "text-emerald-300" : "text-amber-300"}>{valid ? "✓" : "!"}</span>
                          <span>{extractLeaf(path)}</span>
                        </div>
                      </td>
                      <td className="px-3 py-3 text-xs text-[var(--muted)]">{extractSystem(path)}</td>
                      <td className="px-3 py-3">
                        <div className="flex justify-end gap-2">
                          <button className="rounded-md border border-[var(--border)] px-2 py-1 text-xs" disabled={index === 0} onClick={() => move(index, -1)} type="button">
                            Up
                          </button>
                          <button
                            className="rounded-md border border-[var(--border)] px-2 py-1 text-xs"
                            disabled={index === editorPaths.length - 1}
                            onClick={() => move(index, 1)}
                            type="button"
                          >
                            Down
                          </button>
                          <button
                            className="rounded-md border border-rose-300/30 px-2 py-1 text-xs text-rose-100"
                            onClick={() => {
                              setEditorPaths((current) => current.filter((_, currentIndex) => currentIndex !== index));
                            }}
                            type="button"
                          >
                            Remove
                          </button>
                        </div>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          )}
        </div>
      </div>

      {pickerOpen ? (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/50 p-4">
          <div className="flex h-full max-h-[80vh] w-full max-w-3xl flex-col overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)]">
            <div className="flex items-center justify-between gap-4 border-b border-[var(--line)] px-5 py-4">
              <div>
                <h3 className="text-base font-semibold">Add ROMs</h3>
                <p className="text-xs text-[var(--muted)]">Pick ROMs from the current library.</p>
              </div>
              <div className="flex items-center gap-2">
                <button
                  className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-sm font-semibold text-white disabled:cursor-not-allowed disabled:opacity-50"
                  disabled={selectedPaths.length === 0}
                  onClick={addSelectedRoms}
                  type="button"
                >
                  Add {selectedPaths.length}
                </button>
                <button
                  className="rounded-md px-2 py-1 text-sm text-[var(--muted)]"
                  onClick={() => {
                    setPickerOpen(false);
                    setSelectedPaths([]);
                    setPickerSearch("");
                  }}
                  type="button"
                >
                  ✕
                </button>
              </div>
            </div>
            <div className="border-b border-[var(--line)] px-5 py-4">
              <input
                aria-label="Search ROM picker"
                className="h-11 w-full rounded-xl border border-[var(--border)] bg-[var(--bg)] px-4 text-sm"
                onChange={(event) => {
                  setPickerSearch(event.target.value);
                }}
                placeholder="Search ROMs"
                value={pickerSearch}
              />
            </div>
            <div className="flex-1 overflow-auto px-5 py-4">
              {loadingOptions ? (
                <div className="py-8 text-center text-sm text-[var(--muted)]">Loading ROM list...</div>
              ) : filteredOptions.length === 0 ? (
                <div className="py-8 text-center text-sm text-[var(--muted)]">No ROMs match this search.</div>
              ) : (
                <div className="space-y-2">
                  {filteredOptions.map((item) => {
                    const alreadyAdded = editorPaths.includes(item.path);

                    return (
                      <label
                        key={item.path}
                        className={`flex items-center gap-3 rounded-xl border border-[var(--border)] px-4 py-3 text-sm ${
                          alreadyAdded ? "opacity-50" : ""
                        }`}
                      >
                        <input
                          checked={selectedPaths.includes(item.path)}
                          disabled={alreadyAdded}
                          onChange={() => {
                            toggleSelection(item.path);
                          }}
                          type="checkbox"
                        />
                        <div className="min-w-0 flex-1">
                          <p className="truncate font-medium">{item.label}</p>
                          <p className="truncate text-xs text-[var(--muted)]">
                            {item.platform} · {item.path}
                          </p>
                        </div>
                        {alreadyAdded ? <span className="text-xs text-[var(--muted)]">Already added</span> : null}
                      </label>
                    );
                  })}
                </div>
              )}
            </div>
          </div>
        </div>
      ) : null}
    </div>
  );
}
