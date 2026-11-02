# EnshroudedVRM 0.3.3 — guia em português (Brasil)

Jogue Enshrouded com seu avatar VRM. Mod de **lightnill**.

Esta versão funciona com a compilação Steam **26320373** do jogo. A versão **0.3.2** continua disponível apenas para a compilação anterior **26082781**. Não misture arquivos dessas versões.

## Instalação, passo a passo

1. Baixe **EnshroudedVRM-0.3.3.zip**, o pacote completo para jogadores. No GitHub, escolha esse arquivo na seção **Assets**. Os arquivos **Source code (zip)** e **Source code (tar.gz)** são o código-fonte e não são o mod pronto para instalar.
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

Os presets **Game lighting** e **Matte** podem ser aplicados ao modelo inteiro. Também é possível alterar materiais individualmente, herdar o preset geral ou restaurar uma configuração. As roupas e armaduras nativas do jogo não são ajustadas automaticamente ao VRM. Modelos grandes podem reduzir o desempenho.

## Animação da boca / lip sync

O recurso de voz é opcional e deve ser ativado nas configurações F8.

- **Game voice** usa a voz de saída do próprio jogo. É necessária uma sessão com chat de voz ativo. Em Single Player, esse caminho pode não receber áudio. O mute e o push-to-talk do jogo controlam o áudio disponível para essa opção.
- **Local microphone** permite usar a animação da boca offline. **Windows default** segue o microfone padrão do Windows; também é possível escolher um dispositivo. Esse modo é independente do mute e do push-to-talk do jogo.

Ajuste **Strength**, **Sensitivity** e **Smoothing** conforme necessário. O VRM precisa ter expressões de boca compatíveis. Não é preciso calibrar gravando vogais. O processamento é local: o mod não grava nem envia áudio e não transmite dados extras de visemas entre jogadores.

## Atualizar sem perder suas configurações

Feche o jogo e substitua os arquivos do pacote em conjunto. Não apague seus modelos pessoais. As configurações ficam em **%LOCALAPPDATA%\EnshroudedVRM\State**, fora da pasta do jogo. Aguarde um momento depois da última alteração antes de fechar o jogo para permitir o salvamento em segundo plano.

Não use os scripts de inicialização das versões antigas depois de instalar o pacote standalone. Não misture Runtime, shaders, Compatibility ou release-manifest.json de versões diferentes. Para voltar a uma versão anterior, faça antes um backup das configurações.

## Se algo não funcionar

- **F8 não abre:** confirme que vulkan-1.dll está ao lado de enshrouded.exe e que a pasta não ficou duplicada. Tente Fn + F8 em notebooks. Confira se o jogo está na compilação compatível e se outro mod já usa vulkan-1.dll.
- **Só aparece o personagem original:** escolha um .vrm válido em F8 e desative No model replacement. Depois de uma atualização do jogo, uma versão incompatível pode recusar a substituição de propósito. Não remova nem desative as verificações de compatibilidade.
- **A boca não se move:** ative o recurso de voz e confira a fonte. Para testar offline, escolha Local microphone e o dispositivo correto. Para Game voice, use uma sessão com voz ativa e confira o mute/push-to-talk do jogo.
- **Aparece um erro de PowerShell:** provavelmente você está usando um pacote ou launcher antigo. A instalação 0.3.3 inicia pela Steam e não exige liberar scripts.
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

A substituição é local ao seu cliente. Sincronização de avatares entre jogadores, ajustes automáticos da armadura nativa e MToon completo não estão implementados. A validação desta atualização foi feita no desktop; outros computadores e configurações ainda precisam de cobertura.

O código original usa a licença MIT. O Oculus LipSync e o modelo de exemplo têm termos separados, descritos nos avisos do pacote e nas permissões do VRM. Mod não oficial, sem vínculo ou endosso da Keen Games.
