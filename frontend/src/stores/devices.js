import { ref } from 'vue'
import { defineStore } from 'pinia'
import { useAuthStore } from './auth'

export const useDevicesStore = defineStore('devices', () => {
  const devices = ref([])
  const loading = ref(false)
  const error = ref(null)
  const activeDevice = ref(null)

  async function fetchDevices() {
    const authStore = useAuthStore()
    if (!authStore.token) return

    loading.value = true
    error.value = null

    try {
      const response = await fetch('http://localhost:8000/api/v1/devices/me', {
        headers: {
          'Authorization': `Bearer ${authStore.token}`
        }
      })

      if (!response.ok) {
        throw new Error('Ошибка получения данных об устройствах')
      }

      devices.value = await response.json()

      // Если есть устройства и нет активного - активируем первое
      if (devices.value.length > 0 && !activeDevice.value) {
        activeDevice.value = devices.value[0]
      }
    } catch (err) {
      error.value = err.message
      console.error('Ошибка при загрузке устройств:', err)
    } finally {
      loading.value = false
    }
  }

  async function createDevice(name) {
    const authStore = useAuthStore()
    if (!authStore.token) return { success: false, error: 'Не авторизован' }

    try {
      const response = await fetch('http://localhost:8000/api/v1/devices/', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${authStore.token}`
        },
        body: JSON.stringify({ name })
      })

      if (!response.ok) {
        const errorData = await response.json()
        throw new Error(errorData.detail || 'Ошибка создания устройства')
      }

      const newDevice = await response.json()
      devices.value.push(newDevice)

      // Если это первое устройство, делаем его активным
      if (devices.value.length === 1) {
        activeDevice.value = newDevice
      }

      return { success: true, device: newDevice }
    } catch (err) {
      console.error('Ошибка при создании устройства:', err)
      return { success: false, error: err.message }
    }
  }

  function setActiveDevice(device) {
    activeDevice.value = device
  }

  return {
    devices,
    loading,
    error,
    activeDevice,
    fetchDevices,
    createDevice,
    setActiveDevice
  }
})