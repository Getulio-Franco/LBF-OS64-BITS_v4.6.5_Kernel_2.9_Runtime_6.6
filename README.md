# LBF-OS64-BITS_v4.6.5_Kernel_2.9_Runtime_6.6
SISTEMA OPERACIONAL  x86-64 BITS

# TStringGrid v2.0 — Componente de Grade de Dados

> Componente estilo Delphi/VCL desenvolvido do zero para a SDK gráfica do
> **LBF-OS** (Ring 3), com edição inline, ordenação, scroll duplo e
> sincronização pixel-perfect entre clique e desenho.

---

## 1. Visão Geral

O `TStringGrid` é uma grade bidimensional de células de texto (matriz
`[linhas][colunas]`) com linha de cabeçalho e coluna de índice congeladas,
larguras independentes por coluna e suporte completo a interação por mouse
e teclado.

Ele é usado pelo aplicativo **Gerenciador de Processos** (`grid.c`) como
tabela de PIDs, mas foi projetado para ser reutilizável por qualquer app
da SDK.

```
+----+-------------+----------------------+----------+
| #  |     PID     |   NOME DO PROCESSO   |  STATUS  |   <- cabeçalho fixo
+----+-------------+----------------------+----------+
| 1  |     12      |  terminal.elf        | EM EXEC  |
| 2  |     11      |  gfile.elf           | EM EXEC  |   <- linhas de dados
| 3  |     10      |  gpci.elf            | EM EXEC  |      (roláveis)
+----+-------------+----------------------+----------+
  ^col fixa congelada            ^ scroll horizontal por "pulo de bloco"
```

---

## 2. Arquitetura em Camadas

O componente segue o modelo de isolamento do LBF-OS:

| Camada | Arquivos | Responsabilidade |
|---|---|---|
| **Subsistema Gráfico (Ring 3)** | `gui/gui.h`, `gui/gui.c` | Struct espelho `TStringGrid`, desenho (`gui_draw_stringgrid`), ponte de propriedades (`gui_set_prop`) |
| **SDK (Ring 3)** | `sdk/libgui.h`, `sdk/libgui.c` | Struct unificada `TGUIControl`, motor central de eventos (mouse/teclado), helpers de ordenação |
| **Implementação do Componente** | `sdk/TStringGrid.c` | Criação, células, scrolls, edição, proteções, sincronização |
| **Aplicativo de exemplo** | `grid.c` | Tabela de processos usando a API pública |

> **Nota de arquitetura:** todo o componente vive em Ring 3 e usa apenas
> `malloc`/`free` da libc do runtime. O Ring 0 **não conhece** o componente:
> ele recebe apenas o framebuffer final da janela via IPC
> (`OS_IPC_FlipBuffers`). A comunicação interna Ring 3 ↔ subsistema gráfico
> é feita pela ponte `gui_set_prop()` com payloads empacotados em `uint64_t`.

### Fluxo de sincronização

```
 App (grid.c)
   │  GUI_Grid_SetCells / SetColWidth / SetScrollX / SelectCell ...
   ▼
 SDK (TStringGrid.c)          ← guarda os DADOS (matriz GridCells)
   │  gui_set_prop(handle, PROP_*, payload_empacotado)
   ▼
 Subsistema gráfico (gui.c)   ← guarda o ESTADO VISUAL (TStringGrid)
   │  gui_draw_stringgrid()
   ▼
 Buffer da janela ──FLIP──► Ring 0 (apenas pixels)
```

---

## 3. Funcionalidades (v2.0)

### Visual
- **Cabeçalho e coluna de índice congelados** (`FixedRows`/`FixedCols`) com
  borda 3D levantada, estilo Windows clássico;
- **Largura individual por coluna** (`GridColWidths[16]`), sincronizada entre
  o desenho e o mapeamento de clique (fonte única de verdade);
- **Destaque azul** (`0x000080`) da célula selecionada, com texto branco;
- **Indicador de ordenação** (setas ▲/▼) desenhado no cabeçalho;
- **Scrollbars proporcionais** nos dois eixos, com thumb dimensionado pela
  razão conteúdo visível / conteúdo total.

### Comportamento
- **RowCount dinâmico**: o grid desenha apenas as linhas que existem —
  sem "linhas fantasma";
- **Scroll vertical** travado pelas linhas de dados (o cabeçalho nunca rola);
- **Scroll horizontal por "pulo de bloco"** (comportamento Delphi): cada
  clique/tecla esconde ou revela uma coluna inteira; as colunas restantes se
  **empacotam** encostadas na coluna fixa, e o espaço vazio só aparece à
  direita quando acabam as colunas;
- **Edição inline** (TEdit embutido): 2º clique na mesma célula (ou `Enter`)
  abre a caixa de edição com caret; `Enter` confirma, `Esc` cancela,
  `Backspace` apaga; o buffer é memória viva compartilhada com o desenho;
- **Ordenação por clique no cabeçalho**: detecta automaticamente conteúdo
  numérico vs. texto; novo clique inverte a direção (asc/desc);
- **Redimensionamento de coluna**: clique perto da divisória do cabeçalho
  entra em modo resize; o próximo clique define a nova largura (mín. 24px);
- **Auto-scroll de seleção**: selecionar ou editar uma célula fora da tela
  rola o grid automaticamente até ela.

### Proteção e robustez
- `ReadOnly` (navega mas não edita) e `Enabled` (componente inerte/cinza);
- **Capacidade fixa de memória** (`GridAllocatedRows`): a matriz é alocada
  uma única vez na criação; `SetCells`/`SetRowCount` travam nesse limite —
  impossível estourar o heap mesmo com dados hostis;
- 64 caracteres máximos por célula, com truncamento seguro no desenho;
- Limpeza completa de memória em falhas de alocação e na destruição
  (sem ponteiros pendurados no `app->Controls`).

---

## 4. API Pública (SDK)

```c
/* Criação e destruição */
TGUIControl* GUI_CreateStringGrid(TGUIEnvironment* app, int x, int y,
                                  int w, int h, int cols, int rows);
void GUI_DestroyStringGrid(TGUIControl* grid);

/* Células */
void  GUI_Grid_SetCells(TGUIControl* grid, int col, int row, const char* text);
char* GUI_Grid_GetCells(TGUIControl* grid, int col, int row);
void  GUI_Grid_Clear(TGUIControl* grid);

/* Dimensões e linhas */
void GUI_Grid_SetColWidth(TGUIControl* grid, int col, int width);
void GUI_Grid_SetRowHeight(TGUIControl* grid, int height);
void GUI_Grid_SetRowCount(TGUIControl* grid, int rows);   // trava na capacidade

/* Rolagem */
void GUI_Grid_SetScroll(TGUIControl* grid, int value);    // vertical (linhas)
void GUI_Grid_SetScrollX(TGUIControl* grid, int value);   // horizontal (colunas)

/* Seleção */
void GUI_Grid_SelectCell(TGUIControl* grid, int row, int col);
void GUI_Grid_GetSelectedCell(TGUIControl* grid, int* row, int* col);

/* Edição inline */
void GUI_Grid_BeginEdit(TGUIControl* grid, int row, int col);
void GUI_Grid_EndEdit(TGUIControl* grid, bool commit);

/* Proteções */
void GUI_Grid_SetReadOnly(TGUIControl* grid, bool read_only);
void GUI_Grid_SetEnabled(TGUIControl* grid, bool enabled);
```

---

## 5. Protocolo de Propriedades (Ring 3 ↔ Desenho)

Payloads empacotados em `uint64_t` (`(high << 32) | low`):

| Prop | Valor | Payload |
|---|---|---|
| `PROP_STATE` | 9 | `RowCount` |
| `PROP_SCROLL_Y` | 10 | índice da 1ª linha de dados visível |
| `PROP_CAPTION` | 5 | ponteiro da matriz `GridCells` |
| `PROP_READONLY` | 12 | 0/1 |
| `PROP_ENABLED` | 13 | 0/1 |
| `PROP_EDIT_CELL` | 14 | `row \| col<<32` (`0xFFFF…` = encerrar edição) |
| `PROP_EDIT_TEXT` | 15 | ponteiro do buffer de edição vivo |
| `PROP_COL_WIDTH` | 16 | `col<<32 \| width` |
| `PROP_ROW_HEIGHT` | 17 | altura da linha |
| `PROP_SELECTED_CELL` | 18 | `row \| col<<32` (destaque azul) |
| `PROP_SCROLL_X` | 19 | índice da 1ª coluna de dados visível |
| `PROP_SORT` | 20 | `col<<32 \| dir` (1 asc / -1 desc) |

---

## 6. Interação do Usuário

| Entrada | Ação |
|---|---|
| Clique em célula | Seleciona (destaque azul) |
| 2º clique na mesma célula / `Enter` | Abre edição inline |
| `Enter` / clicar fora | Confirma edição |
| `Esc` | Cancela edição |
| `Backspace` | Apaga caractere em edição |
| Clique no cabeçalho | Ordena coluna (▲/▼); repete = inverte |
| Clique na divisória do cabeçalho | Modo redimensionamento |
| Barra vertical / `W`/`S` (↑/↓) | Rola linhas |
| Barra horizontal / `A`/`D` (←/→) | Rola colunas (pulo de bloco) |

---

## 7. Exemplo de Uso

```c
GridProcessos = GUI_CreateStringGrid(&MyApp, 10, 80, 530, 310, 4, MAX + 1);
if (GridProcessos) {
    GUI_Grid_SetColWidth(GridProcessos, 0, 60);    // [#] fixa
    GUI_Grid_SetColWidth(GridProcessos, 1, 110);   // [PID]
    GUI_Grid_SetColWidth(GridProcessos, 2, 330);   // [NOME]
    GUI_Grid_SetColWidth(GridProcessos, 3, 200);   // [STATUS]

    GUI_Grid_SetCells(GridProcessos, 0, 0, "#");
    GUI_Grid_SetCells(GridProcessos, 1, 0, "PID");
    GUI_Grid_SetCells(GridProcessos, 2, 0, "NOME DO PROCESSO");
    GUI_Grid_SetCells(GridProcessos, 3, 0, "STATUS");

    GUI_Grid_SetRowCount(GridProcessos, 2);        // cabeçalho + 1
    GUI_Grid_SetEnabled(GridProcessos, true);
    GUI_Grid_SetReadOnly(GridProcessos, false);    // libera edição
}
```

---

## 8. Histórico de Versões

| Versão | Marcos |
|---|---|
| **v1.0** | Grade básica, células fixas, scroll vertical |
| **v1.3–v1.4** | `RowCount` dinâmico, cabeçalho congelado, scrollbar proporcional |
| **v1.5** | Larguras por coluna, edição inline, `ReadOnly`/`Enabled`, segurança de memória |
| **v2.0** | Destaque de seleção, ordenação ▲▼, scroll horizontal por bloco, resize de coluna, thumb proporcional corrigido |

---

## 9. Limitações Conhecidas / Roadmap

- Máx. 16 colunas e 64 caracteres por célula;
- Resize por dois cliques (sem arrasto contínuo — o IPC ainda não expõe
  eventos de *mouse move* por app);
- Sem seleção múltipla ou merge de células;
- Roadmap: arrasto de divisória com `EVENT_MOUSE_MOVE`, cores por célula,
  scroll suave por pixel.

---

*Desenvolvido em conjunto durante sessão de pair-programming homem–máquina.
Testado em LBF-OS Base_v4.6.5 / Kernel v2.9 / Runtime v6.6 (VirtualBox).*
