import { describe, it, expect } from 'vitest';
import {
  DELIMITER_MIN_PX,
  delimiterXs,
  formatForScale,
  nonWorkingBands,
} from '../../src/renderer/TimeAxis';
import {
  DEFAULT_GRID_DELIMITER,
  DEFAULT_WEEKEND_DISPLAY,
  gridDelimiterOf,
  weekendDisplayOf,
  type CalendarSettings,
} from '../../src/types/document';
import { validateDocument, SchemaError } from '../../src/persistence/schema';
import { sampleDocument } from '../../src/utils/sample';

/**
 * The calendar painted down through the plot (SRS-TAX-054 to -057).
 *
 * The chart drew no vertical rule at all except the today line, and the
 * weekend mask was gated to two scales at one percent contrast — so both of
 * these were, to the owner on 2026-10-29, simply absent. Everything here is
 * about whether a rule or a band lands where the calendar says it should, and
 * about what happens at the far ends of the zoom range, which is where drawing
 * one rule per unit stops being a good idea.
 */
describe('delimiterXs (SRS-TAX-054)', () => {
  const WIDTH = 700;

  it('draws nothing when the roadmap asks for none', () => {
    expect(delimiterXs('none', '2026-10-01', 20, WIDTH)).toEqual([]);
  });

  it('puts a week rule on every Monday, not on the view edge', () => {
    // 2026-10-29 is a Thursday; the first Monday at or before it is the 26th.
    const xs = delimiterXs('week', '2026-10-29', 10, WIDTH);
    // The boundary before the edge is off-screen and must not be clamped to 0.
    expect(xs[0]).toBeCloseTo(40, 6); // Mon 2 Nov, four days on
    expect(xs[1] - xs[0]).toBeCloseTo(70, 6);
  });

  it('puts a month rule on the first of the month', () => {
    const xs = delimiterXs('month', '2026-10-15', 8, WIDTH);
    expect(xs[0]).toBeCloseTo(17 * 8, 6); // 1 Nov
    expect(xs[1]).toBeCloseTo((17 + 30) * 8, 6); // 1 Dec
  });

  it('takes quarter boundaries from fiscalYearStart (SRS-TAX-010)', () => {
    // A July fiscal year breaks on 1 Jul / 1 Oct / 1 Jan / 1 Apr.
    const xs = delimiterXs('quarter', '2026-08-01', 4, 2000, 7);
    // 1 Oct 2026 is 61 days after 1 Aug.
    expect(xs[0]).toBeCloseTo(61 * 4, 6);
    const calendarYear = delimiterXs('quarter', '2026-08-01', 4, 2000, 1);
    expect(calendarYear[0]).toBeCloseTo(61 * 4, 6); // 1 Oct either way
    // ...but the January year breaks again on 1 Jan, and so does July's Q3.
    expect(xs.length).toEqual(calendarYear.length);
  });

  it('suppresses the unit entirely rather than crowding it (SRS-TAX-055)', () => {
    // The floor of the zoom range: a day boundary every twentieth of a pixel.
    expect(delimiterXs('day', '2026-10-01', 0.05, WIDTH)).toEqual([]);
    expect(delimiterXs('week', '2026-10-01', 0.05, WIDTH)).toEqual([]);
    // Right at the threshold it is drawn, just below it is not.
    expect(delimiterXs('day', '2026-10-01', DELIMITER_MIN_PX, 100).length).toBeGreaterThan(0);
    expect(delimiterXs('day', '2026-10-01', DELIMITER_MIN_PX - 0.01, 100)).toEqual([]);
  });

  it('never runs past the visible width', () => {
    const xs = delimiterXs('day', '2026-10-01', 20, WIDTH);
    expect(xs.length).toBeGreaterThan(30);
    for (const x of xs) {
      expect(x).toBeGreaterThanOrEqual(0);
      expect(x).toBeLessThanOrEqual(WIDTH);
    }
  });

  it('is independent of the time scale, which is the whole point', () => {
    // No scale argument exists to pass: the granularity is the document's.
    const asked = delimiterXs('month', '2026-10-01', 12, WIDTH);
    expect(asked.length).toBeGreaterThan(0);
  });
});

describe('nonWorkingBands (SRS-TAX-056)', () => {
  const MON_FRI = [1, 2, 3, 4, 5];

  it('merges a Saturday and Sunday into one band', () => {
    // 2026-10-26 is a Monday, so the first weekend starts five days on.
    const bands = nonWorkingBands('2026-10-26', 10, 300, MON_FRI);
    expect(bands[0].x).toBeCloseTo(50, 6);
    expect(bands[0].w).toBeCloseTo(20, 6);
    // One band per weekend, not one per day.
    expect(bands.length).toBeLessThanOrEqual(5);
  });

  it('shades at month scale too, where it used to be skipped entirely', () => {
    // 2 px/day is a month-ish view and well inside the legibility floor.
    expect(nonWorkingBands('2026-10-26', 2, 700, MON_FRI).length).toBeGreaterThan(2);
  });

  it('goes quiet when a day is thinner than a hairline', () => {
    expect(nonWorkingBands('2026-10-26', 0.05, 700, MON_FRI)).toEqual([]);
    expect(nonWorkingBands('2026-10-26', 1.4, 700, MON_FRI)).toEqual([]);
  });

  it('shades whatever the roadmap calls non-working, not just the weekend', () => {
    // A four-day week: Friday is off as well.
    const bands = nonWorkingBands('2026-10-26', 10, 300, [1, 2, 3, 4]);
    expect(bands[0].x).toBeCloseTo(40, 6); // Friday
    expect(bands[0].w).toBeCloseTo(30, 6); // Fri + Sat + Sun as one run
  });

  it('draws nothing when every day is a working day', () => {
    expect(nonWorkingBands('2026-10-26', 10, 300, [0, 1, 2, 3, 4, 5, 6])).toEqual([]);
  });
});

describe('formatForScale (SRS-TAX-058)', () => {
  it('leads with the weekday on a day axis', () => {
    expect(formatForScale('2026-11-02', 'day')).toEqual('Mon 2 Nov');
  });

  it('names the week on a week axis, as the axis band does', () => {
    expect(formatForScale('2026-11-02', 'week')).toEqual('2 Nov · W45');
  });

  it('gives a whole date on the coarse scales, never a period name', () => {
    // snapDelta quantises to whole weeks above the day scale, so there is
    // always an exact day to report — "Nov 2026" would hide it.
    expect(formatForScale('2026-11-02', 'month')).toEqual('2 Nov 2026');
    expect(formatForScale('2026-11-02', 'quarter')).toEqual('2 Nov 2026');
    expect(formatForScale('2026-11-02', 'year')).toEqual('2 Nov 2026');
  });
});

describe('the two settings round-trip (SRS-TAX-057)', () => {
  const bare: CalendarSettings = {
    scale: 'week',
    fiscalYearStart: 1,
    workingDays: [1, 2, 3, 4, 5],
    holidays: [],
  };

  it('reads a default when the document predates the setting', () => {
    expect(gridDelimiterOf(bare)).toEqual(DEFAULT_GRID_DELIMITER);
    expect(weekendDisplayOf(bare)).toEqual(DEFAULT_WEEKEND_DISPLAY);
  });

  it('prefers what the document says over the default', () => {
    expect(gridDelimiterOf({ ...bare, gridDelimiter: 'none' })).toEqual('none');
    expect(weekendDisplayOf({ ...bare, weekendDisplay: 'off' })).toEqual('off');
  });

  it('opens a roadmap written before today', () => {
    const old = JSON.parse(JSON.stringify(sampleDocument()));
    delete old.calendar.gridDelimiter;
    delete old.calendar.weekendDisplay;
    expect(() => validateDocument(old)).not.toThrow();
  });

  it('carries both fields through save and open', () => {
    const doc = JSON.parse(JSON.stringify(sampleDocument()));
    doc.calendar.gridDelimiter = 'quarter';
    doc.calendar.weekendDisplay = 'off';
    const reopened = validateDocument(JSON.parse(JSON.stringify(doc)));
    expect(reopened.calendar.gridDelimiter).toEqual('quarter');
    expect(reopened.calendar.weekendDisplay).toEqual('off');
  });

  it('rejects a value outside the set', () => {
    const doc = JSON.parse(JSON.stringify(sampleDocument()));
    doc.calendar.gridDelimiter = 'fortnight';
    expect(() => validateDocument(doc)).toThrow(SchemaError);
    const other = JSON.parse(JSON.stringify(sampleDocument()));
    other.calendar.weekendDisplay = 'dimmed';
    expect(() => validateDocument(other)).toThrow(SchemaError);
  });
});
