# Etapa 3 — TheraLink (Projeto Final EmbarcaTech)
**Autores:** Wagner Junior e Pedro Henrique

Este repositório contém a pasta **Etapa 3**, com registro de um vídeo testando o projeto, desafios encontrados e melhorias planejadas.

---

## 🎥 Vídeo (público)
Link: https://youtu.be/Nda5O4mBOp4  

---

## 🧩 O que é o TheraLink
Dispositivo **offline** (BitDogLab / Pico W) que cria **Wi‑Fi próprio**, mede **batimentos (BPM)** no oxímetro e apresenta, numa **página web local**, um retrato rápido de **grupos grandes** (ex.: ~20 alunos). Inclui um marcador **lúdico por cores** e uma escala **1–4** simples para apoiar o profissional na visão geral do grupo — tudo **sem internet**.


## 🧪 Desafios encontrados
- **Ligar o oxímetro e ler dados** do **MAX3010x** com estabilidade.
- **Descobrir por que não funcionava usar os 2 sensores diretamente na BitDog (I²C0 e I²C1):**
  - O **display OLED** utiliza a **entrada I²C1** na BitDogLab.
  - O **sensor de cor TCS34725** estava ligado ao **I²C1**, causando **conflito** com o OLED.
  - A tentativa de manter os **dois sensores diretamente** na BitDog (I²C0 e I²C1) **não funcionou** devido ao conflito no **I²C1**.
  - **Solução:** uso de **extensor I²C**; **MAX3010x** e **TCS34725** no **I²C0** via extensor, e **OLED** permanece no **I²C1**.

---

## 🚀 Melhorias planejadas
- **Interação na BitDog (UX do menu):**
  - **Buzzer** fazer barulho ao alterar números/opções.
  - **Matriz** (LED/OLED) criar **rostos** conforme nível **1–4** (*1 carinha feliz; 4 carinha triste*).
- **Escala e throughput de atendimento:**
  - Utilizar **duas BitDogLab** com o **mesmo código rodando**, enviando **em paralelo** para o profissional, para **acelerar o processo**.
- **Qualidade de medição:**
  - **Melhorar a leitura do BPM** que **não está precisa** (ajuste de filtros e detecção de picos).
- **Instrumentos de avaliação:**
  - **Fazer mais perguntas** (além de ansiedade 1–4) para relatório mais **completo** do status mental do grupo.
- **Arquitetura de software:**
  - Adicionar **FreeRTOS** para **desempenho** e **prioridades** (rede, amostragem, UI).
- **Interface de usuário:**
  - Usar **outro display** no lugar do **OLED** para melhor leitura dos menus (ex.: **tablet**).

---

