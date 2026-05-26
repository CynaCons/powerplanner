import { nanoid } from 'nanoid';

export const newId = (prefix: string): string => `${prefix}-${nanoid(8)}`;
