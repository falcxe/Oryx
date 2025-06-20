import './assets/main.css'

import { createApp } from 'vue'
import { createPinia } from 'pinia'

import App from './App.vue'
import router from './router'

import '@material/web/button/filled-button.js'
import '@material/web/textfield/filled-text-field.js'

const app = createApp(App)

app.use(createPinia())
app.use(router)

app.mount('#app')
