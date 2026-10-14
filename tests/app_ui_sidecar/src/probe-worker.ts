self.addEventListener('message', () => {
  self.postMessage({ ready: true, origin: self.location.origin })
})
