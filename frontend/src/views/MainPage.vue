<template>
  <div id="AuthButtons">
    <md-filled-button @click="$router.push('/login')">Войти</md-filled-button>
    <md-outlined-button @click="$router.push('/register')">Зарегистрироваться</md-outlined-button>
  </div>
  <div class="OnSurfaceCont">
    <div class="MainDescription">
      <h1 id="MainHeader">Oryx</h1>
      <p>
        Умный велосипедный спидометр для отслеживания поездок, сбора данных по Bluetooth и анализа вашей активности. Подключите устройство, начните поездку и получайте подробную статистику прямо на смартфоне!
      </p>
    </div>
    <img
      src="https://images.unsplash.com/photo-1746908592757-2c1d682b71db?q=80&w=1170&auto=format&fit=crop&ixlib=rb-4.1.0&ixid=M3wxMjA3fDB8MHxwaG90by1wYWdlfHx8fGVufDB8fHx8fA%3D%3D"
      alt="Велосипедист"
    />
  </div>
  <footer>
    <p>© 2024 Oryx</p>
  </footer>
</template>

<script setup>
import '@material/web/button/filled-button.js'
import '@material/web/button/outlined-button.js'
</script>

<style scoped>
.OnSurfaceCont {
  display: flex;
  flex-direction: row;
  justify-content: space-between;
  background-color: var(--md-sys-color-primary-container);
  border-radius: var(--md-sys-shape-corner-large);
  padding: 2rem 2.5rem;
  max-width: 65%;
  max-height: 40%;
  margin-left: auto;
  margin-right: auto;
  width: fit-content;
}

.OnSurfaceCont img {
  width: 340px;
  height: 255px;
  object-fit: cover;
  border-radius: 12px;
  box-shadow: 0px 5px 5px -5px rgba(34, 60, 80, 0.6);
}

#AuthButtons {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  margin: 2rem 2.5rem 0 0;
}

.MainDescription {
  max-width: 35%;
}
.OnSurfaceCont p {
  color: var(--md-sys-color-on-primary-container);
}

#MainHeader {
  color: var(--md-sys-color-on-background);
}
footer {
  text-align: center;
  margin-top: 2rem;
  color: #888;
}
</style>
