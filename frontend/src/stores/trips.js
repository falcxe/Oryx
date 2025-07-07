import { ref } from 'vue'
import { defineStore } from 'pinia'
import { useAuthStore } from './auth'

export const useTripsStore = defineStore('trips', () => {
  const trips = ref([])
  const loading = ref(false)
  const error = ref(null)

  async function fetchTrips() {
    const authStore = useAuthStore()
    if (!authStore.token) return

    loading.value = true
    error.value = null

    try {
      const response = await fetch('http://localhost:8000/api/v1/trips/me', {
        headers: {
          'Authorization': `Bearer ${authStore.token}`
        }
      })

      if (!response.ok) {
        throw new Error('Ошибка получения данных о поездках')
      }

      trips.value = await response.json()
    } catch (err) {
      error.value = err.message
      console.error('Ошибка при загрузке поездок:', err)
    } finally {
      loading.value = false
    }
  }

  async function createTrip(tripData) {
    const authStore = useAuthStore()
    if (!authStore.token) return { success: false, error: 'Не авторизован' }

    try {
      const response = await fetch('http://localhost:8000/api/v1/trips/', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${authStore.token}`
        },
        body: JSON.stringify(tripData)
      })

      if (!response.ok) {
        const errorData = await response.json()
        throw new Error(errorData.detail || 'Ошибка создания поездки')
      }

      const newTrip = await response.json()
      trips.value.push(newTrip)
      return { success: true, trip: newTrip }
    } catch (err) {
      console.error('Ошибка при создании поездки:', err)
      return { success: false, error: err.message }
    }
  }

  return {
    trips,
    loading,
    error,
    fetchTrips,
    createTrip
  }
})