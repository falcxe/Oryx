import { ref } from 'vue'
import { defineStore } from 'pinia'

export const useAuthStore = defineStore('auth', () => {
  const user = ref(null)
  const token = ref(localStorage.getItem('token') || null)
  const isAuthenticated = ref(!!token.value)

  async function login(email, password) {
    try {
      const response = await fetch('http://localhost:8000/api/v1/auth/login', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ email, password }),
      })

      if (!response.ok) {
        throw new Error('Ошибка авторизации')
      }

      const data = await response.json()
      token.value = data.access_token
      localStorage.setItem('token', token.value)
      isAuthenticated.value = true

      // Загружаем данные пользователя
      await fetchUserData()

      return { success: true }
    } catch (error) {
      console.error('Ошибка входа:', error)
      return { success: false, error: error.message }
    }
  }

  async function register(email, password) {
    try {
      const response = await fetch('http://localhost:8000/api/v1/auth/register', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ email, password }),
      })

      if (!response.ok) {
        const error = await response.json()
        throw new Error(error.detail || 'Ошибка регистрации')
      }

      return { success: true }
    } catch (error) {
      console.error('Ошибка регистрации:', error)
      return { success: false, error: error.message }
    }
  }

  async function fetchUserData() {
    if (!token.value) return

    try {
      const response = await fetch('http://localhost:8000/api/v1/users/me', {
        headers: {
          'Authorization': `Bearer ${token.value}`
        }
      })

      if (response.ok) {
        user.value = await response.json()
      } else if (response.status === 401) {
        // Если токен недействителен, выходим из системы
        logout()
      }
    } catch (error) {
      console.error('Ошибка получения данных пользователя:', error)
    }
  }

  function logout() {
    user.value = null
    token.value = null
    isAuthenticated.value = false
    localStorage.removeItem('token')
  }

  // При создании стора проверяем наличие токена и загружаем данные пользователя
  if (token.value) {
    fetchUserData()
  }

  return {
    user,
    token,
    isAuthenticated,
    login,
    register,
    logout,
    fetchUserData
  }
})