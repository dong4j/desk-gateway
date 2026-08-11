/** Desk Gateway 移动端原型的统一视觉 token。 */

export const palette = {
  background: '#FAF7F0',
  surface: '#FEFCF8',
  surfaceMuted: '#F3EEE4',
  ink: '#171613',
  inkMuted: '#706A61',
  inkFaint: '#9A9389',
  line: '#DED5C7',
  gold: '#B48752',
  goldSoft: '#DFC7A8',
  green: '#2BBE72',
  greenInk: '#315E42',
  greenSurface: '#F0F2DF',
  danger: '#FF3B30',
  disabled: '#D8D3CA',
  white: '#FFFFFF',
} as const;

export const radii = {
  small: 12,
  medium: 18,
  large: 26,
  pill: 999,
} as const;

export const shadows = {
  floating: {
    shadowColor: '#5E4A31',
    shadowOffset: { width: 0, height: 8 },
    shadowOpacity: 0.12,
    shadowRadius: 18,
    elevation: 4,
  },
} as const;
