const dispatch = (type: string, message: unknown) => {
  window.dispatchEvent(new CustomEvent('fixture-toast', { detail: { type, message: String(message) } }))
}

export const toast = {
  success: (message: unknown) => dispatch('success', message),
  error: (message: unknown) => dispatch('error', message)
}
