import { describe, it, expect, beforeEach } from 'vitest';
import { useToolStore } from '../../src/stores/toolStore';

/**
 * SRS-EDIT-026 — an insert tool reverts once it has inserted one item.
 *
 * `setTool(tool, sticky = true)` with every call site taking the default meant
 * `consumeOneShot`'s `if (!get().sticky)` was never true, so the tool stayed
 * armed forever. The interaction handler was calling `consumeOneShot` all
 * along; the store simply declined to act on it.
 *
 * Observed red on 79ccbdb: after `setTool('add-task')` and `consumeOneShot()`,
 * `activeTool` was still `add-task`.
 */

const reset = () => useToolStore.setState({ activeTool: 'select', sticky: true });

describe('arming a tool', () => {
  beforeEach(reset);

  it('an insert tool armed with no second argument is one-shot', () => {
    useToolStore.getState().setTool('add-task');
    expect(useToolStore.getState().activeTool).toBe('add-task');

    useToolStore.getState().consumeOneShot();
    expect(useToolStore.getState().activeTool).toBe('select');
  });

  it('holds for milestones, which run the same branch', () => {
    useToolStore.getState().setTool('add-milestone');
    useToolStore.getState().consumeOneShot();
    expect(useToolStore.getState().activeTool).toBe('select');
  });

  it('a marquee reverts too, so a lasso cannot become a mode', () => {
    useToolStore.getState().setTool('marquee');
    useToolStore.getState().consumeOneShot();
    expect(useToolStore.getState().activeTool).toBe('select');
  });

  it('pan stays, because panning is a mode the planner works in', () => {
    useToolStore.getState().setTool('pan');
    useToolStore.getState().consumeOneShot();
    expect(useToolStore.getState().activeTool).toBe('pan');
  });

  it('an explicit sticky request is still honoured', () => {
    useToolStore.getState().setTool('add-task', true);
    useToolStore.getState().consumeOneShot();
    expect(useToolStore.getState().activeTool).toBe('add-task');
  });
});
