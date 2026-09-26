# Configuração de VM Windows 11 Pro

Guia para configurar uma máquina virtual Windows 11 Pro usando o mesmo ISO do PC.

---

## Escolha do Hypervisor

| Hypervisor | Prós | Contras |
|---|---|---|
| **VirtualBox** (gratuito) | Leve, não conflita com outros hypervisors, fácil de usar | Performance GPU limitada |
| **VMware Workstation Pro** (gratuito para uso pessoal) | Melhor performance, suporte a DirectX 11, snapshots avançados | Mais pesado que VirtualBox |
| **Hyper-V** (built-in Windows Pro) | Integração nativa, boa performance | Conflita com VirtualBox/VMware, requer reinício para ativar/desativar |

**Recomendação:** VMware Workstation Pro — gratuito para uso pessoal desde 2024, melhor performance geral e não conflita com outros softwares.

---

## Requisitos Mínimos do Host

- **CPU:** 4+ cores (Intel VT-x ou AMD-V habilitado na BIOS)
- **RAM:** 16 GB (mínimo 8 GB dedicados à VM)
- **Disco:** 80 GB livres (SSD recomendado)
- **SO Host:** Windows 10/11 Pro

### Verificar virtualização na BIOS

```
# No PowerShell (admin):
systeminfo | findstr /i "Hyper-V"

# Ou verificar diretamente:
Get-ComputerInfo -Property "HyperV*"
```

Se "Virtualization Enabled In Firmware" estiver **No**, ative na BIOS:
- **Intel:** Advanced > CPU Configuration > Intel Virtualization Technology > Enabled
- **AMD:** Advanced > SVM Mode > Enabled

---

## Opção 1: VMware Workstation Pro (Recomendado)

### 1. Download e Instalação

1. Acesse: https://www.vmware.com/products/desktop-hypervisor/workstation-and-fusion
2. Baixe o VMware Workstation Pro (gratuito para uso pessoal)
3. Instale com as opções padrão

### 2. Desativar Hyper-V (se ativo)

O Hyper-V conflita com VMware. Desative antes:

```powershell
# PowerShell como Administrador
bcdedit /set hypervisorlaunchtype off

# Desativar o recurso
Disable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All

# Reiniciar
Restart-Computer
```

### 3. Criar a VM

1. **File > New Virtual Machine > Custom (advanced)**
2. Hardware Compatibility: **Workstation 17.x** (ou mais recente)
3. **Installer disc image file (ISO):** selecione o mesmo ISO do Windows 11 Pro
4. Configurações recomendadas:

| Componente | Configuração |
|---|---|
| **Firmware** | UEFI + Secure Boot |
| **Processadores** | 4 cores (2 processadores x 2 cores) |
| **RAM** | 8 GB (8192 MB) |
| **Rede** | NAT |
| **Disco** | 80 GB, Store as single file, NVMe |
| **Display** | 3D Acceleration ON, 2 GB VRAM |

### 4. Configuração adicional pós-criação

Edite o arquivo `.vmx` da VM para garantir compatibilidade com TPM 2.0:

```
# No VMware, a VM já inclui vTPM automaticamente.
# Se necessário, adicione manualmente:
# VM > Settings > Add > Trusted Platform Module
```

### 5. Instalar VMware Tools

Após instalar o Windows:
1. **VM > Install VMware Tools**
2. Execute o instalador dentro da VM
3. Reinicie

Isso habilita: resolução dinâmica, clipboard compartilhado, pastas compartilhadas, melhor performance.

---

## Opção 2: VirtualBox

### 1. Download e Instalação

1. Acesse: https://www.virtualbox.org/wiki/Downloads
2. Baixe o VirtualBox + Extension Pack
3. Instale ambos

### 2. Desativar Hyper-V (mesmo procedimento da Opção 1)

### 3. Criar a VM

1. **Machine > New**
2. Configurações:

| Campo | Valor |
|---|---|
| **Name** | Windows 11 Pro |
| **Type** | Microsoft Windows |
| **Version** | Windows 11 (64-bit) |
| **RAM** | 8192 MB |
| **CPU** | 4 cores |
| **Disco** | 80 GB (VDI, dinamicamente alocado) |

### 4. Configurações extras obrigatórias para Windows 11

O Windows 11 exige TPM 2.0 e Secure Boot. No VirtualBox 7+:

```bash
# Settings > System > Motherboard:
#   - Enable EFI
#   - Enable Secure Boot

# Settings > System > Processor:
#   - Enable PAE/NX
#   - Marcar "Enable Nested VT-x/AMD-V" (se disponível)
```

**TPM no VirtualBox (v7.0+):**
- Settings > Security > Enable TPM > TPM 2.0

**Se versão anterior a 7.0**, use o bypass de TPM na instalação:
```
# Durante a instalação, na tela "This PC can't run Windows 11":
# Shift + F10 para abrir CMD, depois:
reg add HKLM\SYSTEM\Setup\LabConfig /v BypassTPMCheck /t REG_DWORD /d 1
reg add HKLM\SYSTEM\Setup\LabConfig /v BypassSecureBootCheck /t REG_DWORD /d 1
reg add HKLM\SYSTEM\Setup\LabConfig /v BypassRAMCheck /t REG_DWORD /d 1
```

### 5. Guest Additions

Após instalar o Windows:
1. **Devices > Insert Guest Additions CD image**
2. Execute `VBoxWindowsAdditions.exe`
3. Reinicie

---

## Configuração Pós-Instalação (qualquer hypervisor)

### Ativação do Windows

Se você tem licença digital vinculada à conta Microsoft:
1. **Settings > System > Activation**
2. Faça login com a mesma conta Microsoft do PC
3. **Troubleshoot > I changed hardware on this device recently**
4. Selecione a licença

> Nota: Uma licença digital só pode estar ativa em um dispositivo por vez. Para a VM, considere comprar uma chave separada ou usar o período de avaliação.

### Otimizações de Performance

```powershell
# Dentro da VM - PowerShell como Admin:

# Desativar efeitos visuais
SystemPropertiesPerformance.exe
# Selecione "Adjust for best performance"

# Desativar Windows Search indexing (economiza I/O)
Stop-Service WSearch
Set-Service WSearch -StartupType Disabled

# Desativar Superfetch/SysMain
Stop-Service SysMain
Set-Service SysMain -StartupType Disabled

# Desativar hibernação (economiza espaço em disco)
powercfg /hibernate off
```

### Snapshots (Pontos de Restauração da VM)

Tire um snapshot logo após instalar e configurar tudo — assim você pode voltar a um estado limpo sem reinstalar:

- **VMware:** VM > Snapshot > Take Snapshot
- **VirtualBox:** Machine > Take Snapshot

---

## Pastas Compartilhadas (Host ↔ VM)

### VMware
1. **VM > Settings > Options > Shared Folders**
2. **Always Enabled > Add**
3. Escolha a pasta do host
4. Acessa na VM via `\\vmware-host\Shared Folders\`

### VirtualBox
1. **Settings > Shared Folders > Add**
2. Escolha a pasta, marque **Auto-mount**
3. Acessa na VM como drive de rede

---

## Dicas

- **Snapshots são seus amigos** — tire um antes de qualquer teste destrutivo
- **Não aloque toda a RAM** — deixe pelo menos 4-6 GB para o host
- **SSD faz diferença** — VMs em HDD são muito lentas
- **Rede NAT vs Bridge:** NAT isola a VM (mais seguro para testes); Bridge coloca na mesma rede do host
- **Clipboard bidirecional:** ative nas Guest Additions/VMware Tools para copiar/colar entre host e VM
