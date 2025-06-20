<template>
  <div>
    <header>
      <h1>Oryx</h1>
    </header>
    <main>
      <h1>Главная страница</h1>
      <p>Добро пожаловать на главную страницу!</p>
      <md-filled-text-field label="Ваше имя" v-model="name" style="margin-bottom: 16px;" />
      <md-filled-button @click="sayHello">Material Кнопка</md-filled-button>
    </main>
    <footer>
      <p>© 2024 My App</p>
    </footer>
  </div>
</template>

<script setup>
import '@material/web/button/filled-button.js'
import '@material/web/textfield/filled-text-field.js'
</script>

<style scoped>

header {
  background: var(--md-sys-color-primary);
  color: var(--md-sys-color-on-primary);
  border-radius: var(--md-sys-shape-corner-large);
  padding: 10px;
  height: 5rem;
  width: 100%;
  display: flex;
  align-items: center;
}

</style>
