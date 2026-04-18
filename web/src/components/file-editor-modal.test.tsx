import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { FileEditorModal } from "./file-editor-modal";

const entry = {
  name: "readme.txt",
  path: "readme.txt",
  type: "file",
  size: 12,
  modified: 1_700_000_000,
  status: "",
  thumbnailPath: "",
};

describe("FileEditorModal", () => {
  afterEach(() => {
    cleanup();
  });

  it("shows a loading state while the file contents are fetched", () => {
    render(
      <FileEditorModal
        entry={entry}
        initialContent=""
        loading
        onCancel={vi.fn()}
        onSave={vi.fn()}
        saving={false}
      />,
    );

    expect(screen.getByText("Loading file contents...")).toBeTruthy();
    expect(screen.getByRole("button", { name: "Save" })).toHaveProperty("disabled", true);
  });

  it("saves the edited content via onSave", () => {
    const onSave = vi.fn();

    render(
      <FileEditorModal
        entry={entry}
        initialContent="hello"
        loading={false}
        onCancel={vi.fn()}
        onSave={onSave}
        saving={false}
      />,
    );

    const textarea = screen.getByLabelText("Edit contents of readme.txt") as HTMLTextAreaElement;

    fireEvent.change(textarea, { target: { value: "updated" } });
    fireEvent.click(screen.getByRole("button", { name: "Save" }));

    expect(onSave).toHaveBeenCalledWith("updated");
  });

  it("surfaces a load error without enabling Save", () => {
    render(
      <FileEditorModal
        entry={entry}
        initialContent=""
        loadError="Could not load file contents."
        loading={false}
        onCancel={vi.fn()}
        onSave={vi.fn()}
        saving={false}
      />,
    );

    expect(screen.getByText("Could not load file contents.")).toBeTruthy();
    expect(screen.getByRole("button", { name: "Save" })).toHaveProperty("disabled", true);
  });
});
