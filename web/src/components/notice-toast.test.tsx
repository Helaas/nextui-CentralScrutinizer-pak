import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { NoticeToast } from "./notice-toast";

describe("NoticeToast", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
    vi.useRealTimers();
  });

  it("auto dismisses after the configured delay", () => {
    const onDismiss = vi.fn();

    vi.useFakeTimers();
    render(<NoticeToast autoDismissMs={1200} message="Renamed file." onDismiss={onDismiss} />);

    expect(screen.getByRole("status")).toBeTruthy();
    expect(onDismiss).not.toHaveBeenCalled();

    vi.advanceTimersByTime(1199);
    expect(onDismiss).not.toHaveBeenCalled();

    vi.advanceTimersByTime(1);
    expect(onDismiss).toHaveBeenCalledTimes(1);
  });

  it("does not reset the timer when the dismiss callback identity changes", () => {
    const firstDismiss = vi.fn();
    const secondDismiss = vi.fn();

    vi.useFakeTimers();
    const { rerender } = render(
      <NoticeToast autoDismissMs={1200} message="Renamed file." onDismiss={firstDismiss} />,
    );

    vi.advanceTimersByTime(800);
    rerender(<NoticeToast autoDismissMs={1200} message="Renamed file." onDismiss={secondDismiss} />);
    vi.advanceTimersByTime(400);

    expect(firstDismiss).not.toHaveBeenCalled();
    expect(secondDismiss).toHaveBeenCalledTimes(1);
  });

  it("keeps manual dismiss available", () => {
    const onDismiss = vi.fn();

    render(<NoticeToast message="Rename failed." onDismiss={onDismiss} />);

    screen.getByRole("button", { name: "Dismiss notice" }).click();

    expect(screen.getByRole("alert")).toBeTruthy();
    expect(onDismiss).toHaveBeenCalledTimes(1);
  });
});
