import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useEditStore } from '../stores/editStore';
import type { LayoutResult } from '../layout/engine';
import { dateToX } from '../layout/timeAxis';
import type { DragPreviewMap } from '../app/useChartInteractions';
import { InlineLabel } from '../app/InlineLabel';

interface Props {
  layout: LayoutResult;
  dragPreview: DragPreviewMap | null;
}

const HANDLE_W = 5;

export function TaskBars({ layout, dragPreview }: Props) {
  const viewStart = useViewportStore((s) => s.startDate);
  const pxPerDay = useViewportStore((s) => s.pxPerDay);
  const isSelected = useSelectionStore((s) => s.isSelected);
  const updateTask = useDocumentStore((s) => s.updateTask);
  const editingTaskId = useEditStore((s) => s.editingTaskId);
  const endEdit = useEditStore((s) => s.endEdit);
  const beginEdit = useEditStore((s) => s.beginEdit);

  return (
    <g>
      {layout.tasks.map((lt) => {
        const t = lt.task;
        const preview = dragPreview?.tasks.get(t.id);
        const start = preview?.start ?? t.start;
        const end = preview?.end ?? t.end;
        const x = dateToX(start, viewStart, pxPerDay);
        const endX = dateToX(end, viewStart, pxPerDay);
        const width = Math.max(2, endX - x + pxPerDay);
        const y = lt.y;
        const h = lt.height;
        const selected = isSelected(t.id);
        const color = t.color || 'var(--bar-default)';
        const pct = Math.max(0, Math.min(100, t.percentComplete ?? 0));
        const fillWidth = (width * pct) / 100;

        return (
          <g key={t.id}>
            {/* Bar body */}
            <rect
              data-pp-kind="task"
              data-pp-id={t.id}
              x={x}
              y={y}
              width={width}
              height={h}
              rx={4}
              fill={color}
              opacity={0.85}
              style={{ cursor: 'grab' }}
            />
            {/* Percent fill */}
            {pct > 0 && (
              <rect
                x={x}
                y={y + h - 4}
                width={fillWidth}
                height={4}
                fill="black"
                opacity={0.45}
                pointerEvents="none"
              />
            )}
            {/* Resize handles */}
            <rect
              data-pp-kind="task"
              data-pp-id={t.id}
              data-pp-sub="start"
              x={x}
              y={y}
              width={HANDLE_W}
              height={h}
              fill="white"
              opacity={selected ? 0.4 : 0.001}
              style={{ cursor: 'ew-resize' }}
            />
            <rect
              data-pp-kind="task"
              data-pp-id={t.id}
              data-pp-sub="end"
              x={x + width - HANDLE_W}
              y={y}
              width={HANDLE_W}
              height={h}
              fill="white"
              opacity={selected ? 0.4 : 0.001}
              style={{ cursor: 'ew-resize' }}
            />
            {/* Selection outline */}
            {selected && (
              <rect
                x={x - 1}
                y={y - 1}
                width={width + 2}
                height={h + 2}
                rx={5}
                fill="none"
                stroke="var(--color-accent)"
                strokeWidth={1.5}
                pointerEvents="none"
              />
            )}
            {/* Inline editor / Label */}
            {editingTaskId === t.id ? (
              <InlineLabel
                text={t.label}
                x={x + 2}
                y={y + 2}
                width={Math.max(80, width - 4)}
                height={h - 4}
                onCommit={(text) => {
                  updateTask(t.id, { label: text });
                  endEdit();
                }}
                onCancel={endEdit}
              />
            ) : (
              <g onDoubleClick={() => beginEdit(t.id)} style={{ cursor: 'text' }}>
                <LabelText
                  text={t.label}
                  x={x}
                  y={y}
                  barWidth={width}
                  barHeight={h}
                  placement={t.labelPlacement ?? 'on-bar'}
                />
              </g>
            )}
          </g>
        );
      })}
    </g>
  );
}

interface LabelProps {
  text: string;
  x: number;
  y: number;
  barWidth: number;
  barHeight: number;
  placement: 'on-bar' | 'left' | 'right' | 'above' | 'below' | 'hidden';
}

function LabelText({ text, x, y, barWidth, barHeight, placement }: LabelProps) {
  if (placement === 'hidden') return null;
  const baseProps = {
    fontSize: 11,
    fill: 'var(--color-text)',
    pointerEvents: 'none' as const,
  };
  if (placement === 'left') {
    return (
      <text x={x - 6} y={y + barHeight / 2 + 4} textAnchor="end" {...baseProps}>
        {text}
      </text>
    );
  }
  if (placement === 'right') {
    return (
      <text x={x + barWidth + 6} y={y + barHeight / 2 + 4} {...baseProps}>
        {text}
      </text>
    );
  }
  if (placement === 'above') {
    return (
      <text x={x + 4} y={y - 4} {...baseProps}>
        {text}
      </text>
    );
  }
  if (placement === 'below') {
    return (
      <text x={x + 4} y={y + barHeight + 12} {...baseProps}>
        {text}
      </text>
    );
  }
  // on-bar
  return (
    <text
      x={x + 8}
      y={y + barHeight / 2 + 4}
      {...baseProps}
      fill="white"
    >
      <tspan>{text}</tspan>
    </text>
  );
}
