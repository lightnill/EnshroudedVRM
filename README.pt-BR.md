# EnshroudedVRM 0.4.0 — guia em português (Brasil)

Jogue Enshrouded com seu avatar VRM. Mod de **lightnill**.

Esta versão funciona com a compilação Steam **26404966** do jogo. A versão **0.3.3** continua disponível para a compilação anterior **26320373**. Não misture arquivos dessas versões.

**A implementação ainda tem bugs e está incompleta.** Os shaders precisam de mais refinamento: reflexos, matcaps, transparência e iluminação fraca podem apresentar resultados incorretos. **A animação da boca por voz de outros jogadores ainda não funciona.** Ainda podem ocorrer travadas e retornos breves ao personagem original.

## Novidades da 0.4.0

- **Modelos por jogador em F8:** escolha You ou Player N e atribua um VRM local. Outros jogadores começam sem substituição. As escolhas são lembradas por conta neste computador, inclusive após reconexão ou troca de personagem.
- **Importador mais completo:** normal maps glTF e mapas/máscaras padrão de matcap/rim do MToon em VRM0/VRM1, com transformações UV suportadas e tangentes geradas. PNGs de 16 bits são convertidos para RGBA8. Isso não reproduz todos os shaders personalizados do Unity nem todos os tipos de máscara.
- **Auto, Enhanced, Fast e Matte:** presets para o modelo inteiro e ajustes individuais por material, salvos por conteúdo do modelo. Auto usa os efeitos suportados quando necessários; materiais sem mapas efetivos mantêm o caminho rápido.
- **Modelos maiores:** removidos os antigos limites pequenos de memória do importador; F8 mostra estimativas de memória das texturas e buffers da CPU. Limites estruturais, do decodificador, da GPU e de alocação continuam valendo. Modelos grandes ainda podem travar o jogo ou esgotar memória.
- **Relatório de depuração local:** diagnósticos recentes, configurações, CPU/RAM/GPU, contexto de renderização e coordenadas dentro do jogo; captura de tela opcional com revisão antes de incluir. Nada é enviado automaticamente.

## Instalação, passo a passo

1. Baixe **EnshroudedVRM-0.4.0.zip**, o pacote completo para jogadores. No GitHub, escolha esse arquivo na seção **Assets**. Os arquivos **Source code (zip)** e **Source code (tar.gz)** são o código-fonte e não são o mod pronto para instalar.
2. **Feche o jogo.** Na biblioteca da Steam, clique com o botão direito em Enshrouded e abra **Gerenciar > Explorar arquivos locais** (em inglês: **Manage > Browse local files**). Encontre a pasta que contém **enshrouded.exe**.
3. Extraia o ZIP para uma pasta temporária. Copie **vulkan-1.dll** e a pasta inteira **EnshroudedVRM** para a mesma pasta de **enshrouded.exe**. Não coloque os arquivos dentro de outra pasta chamada EnshroudedVRM e não copie apenas os arquivos de Runtime.
4. Se já existir um **vulkan-1.dll de outro mod**, pare: não substitua, renomeie nem tente combinar os dois. Se estiver atualizando uma instalação conhecida deste mod, substitua todos os arquivos do pacote com o jogo fechado e preserve seus VRMs pessoais.
5. Inicie o jogo **normalmente pela Steam**. Não é necessário executar um launcher, arquivo .exe adicional, .bat/.cmd, PowerShell ou outro carregador de mods. Não é necessário alterar a política de execução do PowerShell, instalar Python ou executar como administrador.
6. Pressione **F8** para abrir as configurações. Em alguns notebooks, pode ser necessário **Fn + F8**. Escolha **Browse...** ou um modelo da lista e selecione um arquivo **.vrm** válido. O personagem original continua aparecendo até que um modelo seja selecionado e carregado. O exemplo **vroid-sample.vrm** é opcional; não é escolhido automaticamente.

A estrutura deve ficar assim:

    Enshrouded/
      enshrouded.exe                 (já pertence ao jogo)
      vulkan-1.dll                    (fornecido pelo mod)
      EnshroudedVRM/
        Runtime/
        Compatibility/
        Avatars/
        release-manifest.json
        README.txt

LEIA-ME-PT-BR.txt é uma documentação adicional na raiz do ZIP; pode ser guardado onde preferir. Não substitua arquivos do Windows e não copie DLLs de sites aleatórios.

## Configurar o avatar e os materiais

Abra **F8 > Avatar**. Para usar o personagem original, ative **No model replacement**. Desative a opção para voltar ao VRM selecionado. A seleção e as preferências são lembradas entre sessões.

Em **F8 > Avatar > Player**, escolha primeiro o jogador a configurar. Player N é uma identificação anônima da sessão; a escolha salva acompanha a conta. É possível usar modelos diferentes ao mesmo tempo. As atribuições mudam apenas o que este computador desenha, sem enviar modelos aos outros clientes.

Os presets **Auto**, **Enhanced**, **Fast** e **Matte** podem ser aplicados ao modelo inteiro. Auto ativa normal maps e máscaras MToon suportadas quando o material precisa; Fast mantém o caminho otimizado anterior. Tente Fast se os reflexos ou a iluminação parecerem errados. Matte ainda pode manter alguns efeitos refletivos do material. Os presets são compartilhados entre todos os jogadores que usam o mesmo conteúdo de modelo. Também é possível alterar materiais individualmente, herdar o preset geral ou restaurar uma configuração. As roupas e armaduras nativas do jogo não são ajustadas automaticamente ao VRM. Modelos grandes podem reduzir o desempenho.

## Animação da boca / lip sync

O recurso de voz é opcional e deve ser ativado nas configurações F8.

- **Game voice** usa a voz de saída do próprio jogo. É necessária uma sessão com chat de voz ativo. Em Single Player, esse caminho pode não receber áudio. O mute e o push-to-talk do jogo controlam o áudio disponível para essa opção.
- **Local microphone** permite usar a animação da boca offline. **Windows default** segue o microfone padrão do Windows; também é possível escolher um dispositivo. Esse modo é independente do mute e do push-to-talk do jogo.

Ajuste **Strength**, **Sensitivity** e **Smoothing** conforme necessário. O VRM precisa ter expressões de boca compatíveis. Não é preciso calibrar gravando vogais. **A voz anima apenas You, mesmo com outro jogador selecionado no menu. Visemas/lip sync de outros jogadores ainda não estão implementados.** O processamento é local: o mod não grava nem envia áudio e não transmite dados extras de visemas entre jogadores.

## Atualizar sem perder suas configurações

Feche o jogo e substitua os arquivos do pacote em conjunto. Não apague seus modelos pessoais. Não compartilhe **player-model-assignments.json**: ele contém tokens privados desta instalação e caminhos locais. Prefira o relatório criado em F8.

As configurações ficam em **%LOCALAPPDATA%\EnshroudedVRM\State**, fora da pasta do jogo. Aguarde um momento depois da última alteração antes de fechar o jogo para permitir o salvamento em segundo plano.

Não use os scripts de inicialização das versões antigas depois de instalar o pacote standalone. Não misture Runtime, shaders, Compatibility ou release-manifest.json de versões diferentes. Para voltar a uma versão anterior, faça antes um backup das configurações.

## Se algo não funcionar

- **F8 não abre:** confirme que vulkan-1.dll está ao lado de enshrouded.exe e que a pasta não ficou duplicada. Tente Fn + F8 em notebooks. Confira se o jogo está na compilação compatível e se outro mod já usa vulkan-1.dll.
- **Só aparece o personagem original:** escolha um .vrm válido em F8 e desative No model replacement. Depois de uma atualização do jogo, uma versão incompatível pode recusar a substituição de propósito. Não remova nem desative as verificações de compatibilidade.
- **A boca não se move:** ative o recurso de voz e confira a fonte. Para testar offline, escolha Local microphone e o dispositivo correto. Para Game voice, use uma sessão com voz ativa e confira o mute/push-to-talk do jogo.
- **Aparece um erro de PowerShell:** provavelmente você está usando um pacote ou launcher antigo. A instalação 0.4.0 inicia pela Steam e não exige liberar scripts.
- **Travadas ou modelo piscando:** anote o que estava acontecendo, o modelo usado e a configuração de neblina. Em F8, crie um relatório de depuração para ajudar a investigar. Não há garantia de FPS igual em todos os modelos ou computadores.
- **Antivírus bloqueou um arquivo:** não desative a proteção. Confira a origem do download e os resultados de verificação publicados para os hashes exatos. Um resultado sem detecções não é uma garantia absoluta de segurança.

## Criar um relatório sem enviar dados automaticamente

Use **F8 > Create debug report** e depois **Open reports folder**. O ZIP é salvo em **%LOCALAPPDATA%\EnshroudedVRM\State\Reports**. Nada é enviado automaticamente.

O relatório reconstrói campos permitidos de diagnóstico, configurações e hardware (CPU, RAM, GPU/driver), estatísticas anônimas do modelo e contexto de renderização. Coordenadas incluídas são apenas a posição **dentro do mapa de Enshrouded**, quando há uma amostra local válida e recente. Não é consultada sua localização no mundo real e não são extraídos saves ou blocos de voxels.

Capturas de tela ficam desativadas a cada novo relatório. Se solicitar uma captura, revise a imagem mostrada e escolha **Include this screenshot** ou **Save without screenshot**. Nomes ou mensagens visíveis continuam visíveis na imagem aprovada. O relatório não anexa áudio, modelos, dumps de memória, logs brutos, caminhos pessoais ou identificadores de conta/servidor. Revise o ZIP antes de compartilhá-lo manualmente.

A fonte de voz salva representa a seleção naquele momento; o relatório ainda não inclui o histórico de trocas de fonte. Não publique o runtime.log bruto sem revisá-lo, pois ele pode conter informações privadas.

## Remover o mod

Com o jogo fechado, remova apenas o vulkan-1.dll e os arquivos fornecidos por este pacote. Guarde antes seus VRMs pessoais. As configurações na pasta do usuário permanecem. O mod não substitui o executável nem os arquivos de dados do jogo.

## Limites e licenças

A substituição é local ao seu cliente. Duas substituições simultâneas foram verificadas no desktop e no notebook na compilação suportada, mas isso não garante estabilidade nem FPS. Há retornos breves ao personagem nativo, atrasos iniciais e picos de tempo de quadro. Um relatório do notebook não conseguiu coletar as configurações gráficas. Reflexos e iluminação fraca ainda precisam de ajustes. Shaders personalizados do Unity, outlines, animação UV, máscaras adicionais de brilho/rugosidade, MToon completo, ajuste da armadura nativa e lip sync remoto ainda não são suportados integralmente. A prévia do inventário continua sendo local; sobreviventes não devem receber o modelo do jogador.

O código original usa a licença MIT. O Oculus LipSync e o modelo de exemplo têm termos separados, descritos nos avisos do pacote e nas permissões do VRM. Mod não oficial, sem vínculo ou endosso da Keen Games.
