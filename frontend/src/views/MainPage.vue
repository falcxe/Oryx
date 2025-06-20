<template>
  <div class="OnSurfaceCont">
    <div class="MainDescription">
      <h1 id="MainHeader">Oryx</h1>
      <p>
        Lorem ipsum, dolor sit amet consectetur adipisicing elit. Labore odit doloremque neque
        facere placeat numquam vel recusandae hic harum rerum. Distinctio vel consequatur debitis
        aut eius nisi excepturi minus laboriosam!
      </p>
    </div>
    <img src="@/assets/hero.jpg" alt="bike" />
  </div>
  <footer>
    <p>© 2024 My App</p>
  </footer>
</template>

<script setup>
import '@material/web/button/filled-button.js'
import '@material/web/textfield/filled-text-field.js'
</script>

<style scoped>
.OnSurfaceCont {
  display: flex;
  flex-direction: row;
  margin-left: auto;
  margin-right: auto;
  width: fit-content;
  background-color: var(--md-sys-color-primary-container);
  border-radius: var(--md-sys-shape-corner-extra-large);
  padding: 1.5rem 3rem;
  max-width: 50vw;
}

.OnSurfaceCont img {
  width: 300px;
  height: 220px;
  object-fit: cover;
  border-radius: 12px;
}

.MainDescription {
  max-width: 35%;
  margin-bottom: 30px;
}
.OnSurfaceCont p {
  color: var(--md-sys-color-on-primary-container);
}

#MainHeader {
  color: var(--md-sys-color-on-background);
}
</style>
