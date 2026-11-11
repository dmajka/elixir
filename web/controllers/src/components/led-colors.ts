export const LED_COLORS: Record<number, { name: string; hex: string }> = {
  0: { name: 'Off', hex: '#45494e' }, 3: { name: 'White', hex: '#e5e8eb' },
  5: { name: 'Red', hex: '#f26370' }, 9: { name: 'Orange', hex: '#f5ad66' },
  13: { name: 'Yellow', hex: '#f3d76b' }, 17: { name: 'Lime', hex: '#b9df6a' },
  21: { name: 'Green', hex: '#70dc91' }, 25: { name: 'Mint', hex: '#6ce0bd' },
  29: { name: 'Turquoise', hex: '#67d7d9' }, 33: { name: 'Sky blue', hex: '#65bce9' },
  37: { name: 'Blue', hex: '#7b9bf4' }, 41: { name: 'Violet', hex: '#a48be9' },
  45: { name: 'Purple', hex: '#cf88e4' }, 49: { name: 'Pink', hex: '#eb8ac5' },
  53: { name: 'Rose', hex: '#f18ca0' }, 57: { name: 'Peach', hex: '#ffb2a2' },
}
export const PARAMETER_DRAG_TYPE = 'application/x-elixir-parameter'
