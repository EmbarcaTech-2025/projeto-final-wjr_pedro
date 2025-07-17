# Mood Mirror Duo

**Autores:** Wagner Junior e Pedro Henrique  
**EmbarcaTech - Brasília**

## 🧠 Problema a ser resolvido

Em comunidades carentes, o acesso à saúde mental é limitado. Psicólogos precisam iniciar sessões de grupo sem saber como os participantes estão emocionalmente. O Mood Mirror Duo resolve isso com um sistema portátil, offline e anônimo que coleta auto-relato de humor e sinais fisiológicos, exibindo um painel visual para o terapeuta adaptar a sessão em tempo real.

## ⚙️ Como funciona (simulação prática)

1. **Participante conecta ao Wi-Fi local da BitDog-A.**  
   Página web é exibida com escala de humor e controle de ansiedade.
2. **Preenche as informações e encosta o dedo no sensor (MAX30100).**
3. **Escolhe uma cor que representa sua emoção e passa no sensor TCS34725.**
4. **Os dados são enviados para a BitDog-B.**  
   A terapeuta visualiza no OLED e matriz de LED um resumo do grupo.
5. **Dados são salvos no microSD para análise posterior.**

## ✅ Requisitos Funcionais (RF)

- RF01: Interface web com escala de humor e controle de ansiedade
- RF02: Detecção de dedo via VL53L0X
- RF03: Leitura de pulso e SpO₂ com MAX30100
- RF04: Leitura de cor via TCS34725
- RF05: Comunicação entre BitDog-A e BitDog-B
- RF06: Visualização de dados em tempo real (OLED/matriz)
- RF07: Logging dos dados em cartão microSD

## 🧩 Requisitos Não Funcionais (RNF)

- RNF01: 100% offline
- RNF02: Portátil (bateria ≥ 4h)
- RNF03: Tempo de interação < 30s
- RNF04: Interface inclusiva
- RNF05: Dados anônimos (hash)
- RNF06: Baixo custo

## 📦 Lista de Materiais

- 2 × BitDogLab (com OLED, LED, buzzer, Wi-Fi)
- 1 × MAX30100
- 1 × VL53L0X
- 1 × TCS34725
- 1 × microSD + adaptador
- Fios, fonte de energia (power bank), adesivos e cartões coloridos


