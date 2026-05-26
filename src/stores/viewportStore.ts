import { create } from 'zustand';
import type { ISODate } from '../types/document';
import { addDays } from '../utils/dates';

interface ViewportState {
  startDate: ISODate;
  pxPerDay: number;
  setStart: (iso: ISODate) => void;
  setPxPerDay: (px: number) => void;
  pan: (dx: number) => void;
  zoom: (factor: number, anchorPx: number) => void;
  fit: (start: ISODate, end: ISODate, widthPx: number) => void;
}

export const useViewportStore = create<ViewportState>((set, get) => ({
  startDate: '2026-05-25',
  pxPerDay: 8,
  setStart: (iso) => set({ startDate: iso }),
  setPxPerDay: (px) => set({ pxPerDay: Math.max(0.05, Math.min(120, px)) }),
  pan: (dx) => {
    const s = get();
    const days = dx / s.pxPerDay;
    set({ startDate: addDays(s.startDate, -Math.round(days)) });
  },
  zoom: (factor, anchorPx) => {
    const s = get();
    const anchorDays = anchorPx / s.pxPerDay;
    const anchorDate = addDays(s.startDate, Math.round(anchorDays));
    const newPx = Math.max(0.05, Math.min(120, s.pxPerDay * factor));
    const newAnchorDays = anchorPx / newPx;
    const newStart = addDays(anchorDate, -Math.round(newAnchorDays));
    set({ pxPerDay: newPx, startDate: newStart });
  },
  fit: (start, end, widthPx) => {
    const days = Math.max(1, Math.round((new Date(end).getTime() - new Date(start).getTime()) / 86_400_000));
    const padding = Math.ceil(days * 0.1);
    const px = widthPx / (days + padding * 2);
    set({ startDate: addDays(start, -padding), pxPerDay: Math.max(0.05, Math.min(120, px)) });
  },
}));
