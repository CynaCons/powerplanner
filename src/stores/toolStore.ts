import { create } from 'zustand';

export type Tool = 'select' | 'add-task' | 'add-milestone' | 'pan' | 'marquee';

interface ToolState {
  activeTool: Tool;
  sticky: boolean;
  /** Set the active tool. sticky=true keeps the tool active until changed. sticky=false reverts to select after one action. */
  setTool: (tool: Tool, sticky?: boolean) => void;
  /** Called by interaction handlers after a successful action; reverts to select if non-sticky. */
  consumeOneShot: () => void;
}

export const useToolStore = create<ToolState>((set, get) => ({
  activeTool: 'select',
  sticky: true,
  setTool: (tool, sticky = true) => set({ activeTool: tool, sticky }),
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
