import { create } from 'zustand';

export type Tool = 'select' | 'add-task' | 'add-milestone' | 'pan' | 'marquee';

interface ToolState {
  activeTool: Tool;
  sticky: boolean;
  /**
   * Set the active tool. `sticky` keeps it armed until something changes it;
   * otherwise it reverts to select after one action. Omitted, only `pan` is
   * sticky — see `setTool` below for why that default matters.
   */
  setTool: (tool: Tool, sticky?: boolean) => void;
  /** Called by interaction handlers after a successful action; reverts to select if non-sticky. */
  consumeOneShot: () => void;
}

export const useToolStore = create<ToolState>((set, get) => ({
  activeTool: 'select',
  sticky: true,
  /**
   * One insert is one insert (SRS-EDIT-026).
   *
   * This defaulted to `sticky = true` and every caller took the default, so
   * `consumeOneShot`'s `if (!get().sticky)` was never true and the tool stayed
   * armed for good. The interaction handler had been calling `consumeOneShot`
   * correctly all along; the store declined to act on it. Pressing `T` armed
   * task insertion permanently, three clicks meant to deselect made three
   * tasks, and the armed tool owned the background click, so there was no way
   * to deselect by clicking at all.
   *
   * Pan is the exception because it is a mode the planner works in rather than
   * an action: you pan, and pan again, until you stop.
   */
  setTool: (tool, sticky = tool === 'pan') => set({ activeTool: tool, sticky }),
  consumeOneShot: () => {
    if (!get().sticky) set({ activeTool: 'select', sticky: true });
  },
}));

/**
 * The cursor for the empty surface under each tool (SRS-EDIT-030).
 *
 * Elements set their own, so this is only what the background says. Pan is the
 * one tool that keeps a hand: it is the tool that moves the surface, which is
 * the gesture the hand has always meant.
 */
export function cursorForTool(tool: Tool): string {
  switch (tool) {
    case 'select':
      return 'default';
    case 'add-task':
    case 'add-milestone':
    case 'marquee':
      return 'crosshair';
    case 'pan':
      return 'grab';
  }
}
