# SimpleHypervisor — Monitor de RtlPcToFileHeader e MmGetSystemRoutineAddress

Hypervisor mínimo baseado em Intel VT-x que intercepta duas funções do kernel Windows via EPT (Extended Page Tables) e loga todas as chamadas.

## O que faz

1. **RtlPcToFileHeader** — loga quem chamou (RIP do caller, return address) e qual endereço (PC) queria resolver
2. **MmGetSystemRoutineAddress** — intercepta toda chamada e loga o nome da rotina sendo buscada

## Arquitetura

```
┌─────────────────────────────────────────────┐
│  User-mode: hvlog.exe (client.c)            │
│  Lê eventos via IOCTL do device driver      │
└─────────────┬───────────────────────────────┘
              │ IOCTL_READ_LOG
┌─────────────▼───────────────────────────────┐
│  Kernel Driver (driver.c)                   │
│  ├── Device \Device\SimpleHypervisor        │
│  ├── Ring-buffer de LOG_ENTRY (log.c)       │
│  └── VMX init + EPT hooks                  │
├─────────────────────────────────────────────┤
│  VMX Root Mode (vmx.c + vmx_asm.asm)       │
│  ├── VM-exit handler                        │
│  │   ├── EPT Violation → detecta hook       │
│  │   ├── Loga evento no ring buffer         │
│  │   └── Single-step via MTF + re-hook      │
│  └── EPT (ept.c)                            │
│      ├── Identity-mapped 512GB (2MB pages)  │
│      ├── Split 2MB→4KB nas páginas-alvo     │
│      └── Remove EXECUTE bit = trap on exec  │
└─────────────────────────────────────────────┘
```

## Como funciona o hook EPT

1. Na inicialização, o hypervisor cria tabelas EPT identity-mapped (GPA == HPA)
2. Localiza as páginas físicas de `RtlPcToFileHeader` e `MmGetSystemRoutineAddress`
3. Faz split de 2MB para 4KB nessas páginas
4. Remove o bit **Execute** da PTE correspondente
5. Quando o guest tenta executar o código nessa página → **EPT Violation** (VM-exit)
6. O handler loga o evento (caller RIP, PID, TID, argumento)
7. Restaura Execute, ativa **Monitor Trap Flag** para single-step uma instrução
8. Após o MTF exit, remove Execute novamente — hook reativado

## Estrutura de log

```c
typedef struct _LOG_ENTRY {
    LARGE_INTEGER   Timestamp;
    LOG_EVENT_TYPE  Type;         // 1=RtlPcToFileHeader, 2=MmGetSystemRoutineAddress
    UINT32          ProcessId;
    UINT32          ThreadId;
    UINT64          CallerRip;
    UINT64          ReturnAddress;
    union {
        struct { UINT64 PcAddress; }       RtlPcToFileHeader;
        struct { WCHAR RoutineName[64]; }  MmGetSystemRoutine;
    } Detail;
} LOG_ENTRY;
```

## Build

### Pré-requisitos
- Windows Driver Kit (WDK) — Visual Studio integration
- Visual Studio 2019/2022 com workload "Desktop development with C++"

### Driver
```
msbuild hypervisor.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Cliente (user-mode)
```
cl src/client.c /Fe:hvlog.exe
```

## Uso

```powershell
# 1. Carregar o driver (requer Test Signing ou Secure Boot desabilitado)
sc create SimpleHV type= kernel binPath= C:\path\to\SimpleHypervisor.sys
sc start SimpleHV

# 2. Executar o leitor de logs
hvlog.exe              # lê os eventos acumulados e sai
hvlog.exe --follow     # modo contínuo (como tail -f)

# 3. Parar
sc stop SimpleHV
sc delete SimpleHV
```

## Exemplo de saída

```
========================================
  SimpleHypervisor Log Reader
========================================

[    1] RtlPcToFileHeader               PID=4     TID=128
        Caller RIP:     0xFFFFF80512345678
        Return Address: 0xFFFFF80512345680
        PC Address:    0xFFFFF80512340000
        Time:          2026-09-26 15:30:01.234

[    2] MmGetSystemRoutineAddress        PID=1234  TID=5678
        Caller RIP:     0xFFFFF805AABBCCDD
        Return Address: 0xFFFFF805AABBCCE0
        Routine Name:  NtCreateFile
        Time:          2026-09-26 15:30:01.567
```

## IOCTLs

| IOCTL | Código | Descrição |
|-------|--------|-----------|
| `IOCTL_READ_LOG` | `0x800` | Lê N entradas do ring buffer |
| `IOCTL_CLEAR_LOG` | `0x801` | Limpa o ring buffer |

## Limitações

- Requer CPU Intel com VT-x e EPT
- Requer modo Test Signing ou driver assinado
- Mapeamento EPT cobre apenas os primeiros 512GB de memória física
- Máximo de 8 hooks simultâneos (facilmente aumentável)
- O log é um ring buffer de 4096 entradas — entradas antigas são sobrescritas

## Segurança

Este projeto é para **pesquisa e estudo** de virtualização e instrumentação de kernel.
Não é um produto de segurança e não deve ser usado em produção.
