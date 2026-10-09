# 05. 네트워크 개요 — NETC 와 EtherCAT

> 이 보드의 핵심 기능인 Ethernet(TSN 스위치)과 EtherCAT 이 칩 안에서 어떻게 구성되고, 보드에서 어떤 PHY 와 커넥터로 나오는지 정리한다. 네트워크 대역(로드맵 50~57)의 출발점이다.
> 관련: [03-board-mapping.md](03-board-mapping.md) · [02-memory-map.md](02-memory-map.md)
>
> 출처: RM 53장 Ethernet Controller (NETC) 53.2, 53.4.1 / 55장 EtherCAT Controller (eCAT) 55.2 / UM12450 2.4, 2.5, Table 4 (J12, J13, J17, J18) / 회로도 7페이지 "ECAT/ETH Option".

![네트워크 구성](images/network-topology.svg)

---

## 1. 칩 안의 두 블록

| 블록 | 무엇인가 | 레지스터 |
|---|---|---|
| **NETC** | TSN 지원 **Ethernet 스위치** + NIC(**ENETC**) + 1588 타이머 + MDIO | `0x6000_0000` (16 MB) |
| **eCAT** | Beckhoff 방식 **EtherCAT 서브디바이스 컨트롤러(ESC)**, 포트 2개 | 주변장치 영역 |

### NETC 는 PCIe 처럼 보인다

RM 53.2 에 따르면 NETC 는 **PCIe Root Complex Integrated Endpoint** 로 자신을 드러낸다. 실제 PCIe 링크는 없다. 레지스터가 PCIe 설정 공간 규약으로 배치되어 있고, 인터럽트는 **MSI-X** 로 온다. 베어메탈에서도 쓸 수 있다. 그러려면 PCIe 열거 대신 정해진 주소의 기능 블록을 직접 초기화해야 하는데, RM 의 *"Use of Integrated End-Points by Non-PCIe Aware Operating Systems"* 절이 그 방법이다.

| 기능 블록 | 역할 |
|---|---|
| Switch Core | 802.1Q 스위치, 8 트래픽 클래스, TSN (Qbv, Qci, Qav, FRER 등) |
| ENETC | 호스트 NIC. 메모리의 **디스크립터 링**으로 패킷 송수신 (DMA) |
| Pseudo MAC | 스위치와 ENETC 를 MAC 없이 직결 (Port 4) |
| Timer | IEEE 1588 / 802.1AS |
| EMDIO | 외부 PHY 관리 (MDC/MDIO) |

> UM 2.4: *"ETH0 와 ETH2 는 **스위치 포트**이고 ETH4 는 ENETC0 포트다."* 보드에 나온 두 Ethernet 포트는 모두 스위치를 거친다. 그래서 단일 포트로 핑만 하려 해도 **스위치 설정이 필요하다.** 단순한 MAC 하나로 시작할 수 있는 칩이 아니다.

### eCAT

RM 55.2 에 따르면 ESC 는 다음 블록으로 구성된다. 슬레이브 애플리케이션은 **PDI(온칩 버스)** 로 ESC 메모리에 접근한다.

- 포트 2개, Auto-forwarder, Loopback
- FMMU, SyncManager
- Distributed Clocks (SYNC/LATCH)
- I2C EEPROM(SII) 인터페이스
- Process RAM

## 2. 보드의 PHY 와 커넥터

| MCU 포트 | PHY | 인터페이스 | 크리스탈 | 커넥터 | 기본 연결 |
|---|---|---|---|---|---|
| **ECAT0** | U22 **RTL8201FI** | MII 100M | Y5 25 MHz | **J57A** | **연결 (기본)** |
| **ECAT1** | U23 **RTL8201FI** | MII 100M | Y6 25 MHz | **J57B** | **연결 (기본)** |
| ETH0 (스위치 포트 0) | U21 **YT8531SH** | RGMII 1G | Y4 25 MHz | J56A | 점퍼 변경 필요 |
| ETH2 (스위치 포트 2) | U29 **YT8531SH** | RGMII 1G | Y7 25 MHz | J56B | 점퍼 변경 필요 |

### 핀 공유 — 둘 중 하나만

**ECAT0 ↔ ETH0**, **ECAT1 ↔ ETH2** 는 MCU 에서 같은 핀을 쓴다. 보드는 아날로그 스위치 TMUX136 6개씩으로 신호를 PHY 둘 중 하나에 보낸다.

| 경로 | SEL 점퍼 | OE 점퍼 | 결과 |
|---|---|---|---|
| ECAT0 / ETH0 | **J12 2-3** (기본) | **J13 2-3** (기본) | ECAT0 → J57A |
| | J12 1-2 | J13 2-3 | ETH0 → J56A |
| ECAT1 / ETH2 | **J18 2-3** (기본) | **J17 2-3** (기본) | ECAT1 → J57B |
| | J18 1-2 | J17 2-3 | ETH2 → J56B |

OE 점퍼(J13/J17)를 1-2 로 두면 먹스가 꺼져 두 PHY 모두 끊긴다. MCU 쪽 IOMUX 도 ECAT 기능과 ETH 기능 중 하나로 맞춰야 한다. 점퍼와 IOMUX 가 어긋나면 링크가 올라오지 않는다.

> 회로도 문구 *"J18(1-2)/J17(2-3): ETH2 Function Via J56B"* 와 UM Table 4 는 일치한다. 실제 보드에서 확인한 뒤 이 문서에 확인 표시를 한다.

### 그래서 순서가 정해진다

1. **기본 점퍼 그대로 EtherCAT 부터** 시작하거나,
2. 점퍼 하나를 옮겨(예: J12 1-2) **ETH0 1G 로 Ethernet 부터** 시작한다.

동시에 쓸 수 있는 조합은 **ECAT0 + ETH2** 또는 **ETH0 + ECAT1** 이다. 로드맵 57 에서 다룬다.

## 3. 소프트웨어 관점의 난이도

| 항목 | 내용 | 판단 |
|---|---|---|
| NETC 레지스터 | PCIe 설정 공간 + 디스크립터 링 + 스위치 명령 링. RM 53장만 1500 쪽 | 직접 작성은 비현실적. **SDK `fsl_netc` 드라이버(BSD-3)를 vendoring** 하는 쪽이 맞다 |
| PHY | MDIO 로 RTL8201 / YT8531 레지스터 설정 | 직접 작성 (작다) |
| TCP/IP | lwIP | 기존 프로젝트(NUCLEO-C5A3ZG `src/lib/lwip`) 방식 재사용 |
| ESC 하드웨어 | 레지스터, SII EEPROM, PDI | RM 55장 + ETG.1000 |
| EtherCAT 스택 | Beckhoff SSC 는 ETG 회원 전용. 오픈소스는 **SOES**(rt-labs) | 라이선스 확인 후 결정 |

## 4. 로드맵 (50 대역)

| 번호 | 내용 | 점퍼 |
|---|---|---|
| 50 | MDIO 로 PHY 4개 ID 읽기, 링크 상태 | 그대로 |
| 51 | NETC 초기화 + ETH0 단일 포트 송수신 (스위치 경유) | J12 1-2 |
| 52 | lwIP — ping, UDP | J12 1-2 |
| 53 | ETH2 추가, 두 포트 스위칭 | J12, J18 1-2 |
| 55 | EtherCAT ESC 기초 — 레지스터, SII EEPROM, 링크 | 기본 |
| 56 | EtherCAT 서브디바이스 스택 (SOES), 마스터에서 OP 진입 | 기본 |
| 57 | Ethernet + EtherCAT 동시 (ECAT0 + ETH2) | J18 1-2 |

### 시험 환경 — PC 한 대로 확인할 수 있는 것만 한다

| 단계 | 상대 | 비고 |
|---|---|---|
| 50~53 | PC (일반 NIC) + 케이블 | ping, UDP. 53 은 PC 쪽 포트가 둘이거나 PC 두 대 |
| 55~56 | PC + **EtherCAT 마스터 소프트웨어** (SOEM 등) | 이 보드는 서브디바이스다. 마스터가 있어야 OP 까지 간다. 일반 NIC 로 된다 |
| 57 | 위 둘 | |

### 구현에서 제외한 것

| 항목 | 이유 |
|---|---|
| **TSN** — 802.1AS(gPTP) 시간 동기, Qbv 스케줄링, Qci, FRER | 그랜드마스터나, 하드웨어 타임스탬프를 지원하는 상대 노드가 있어야 한다. 예를 들면 Linux + i210/i226 NIC + linuxptp 나 TSN 보드 한 장이다. 지금 장비로는 동작을 검증할 수 없다. 스위치는 일반 802.1Q 스위치로만 쓴다 |

필요한 장비가 생기면 54 번을 다시 연다.

## 5. 확인 필요

- [ ] PHY MDIO 주소 4개 (회로도 10~13페이지 strap)
- [ ] ECAT 쪽 SII EEPROM 실장 여부와 위치
- [ ] SOES 라이선스와 ESC 지원 범위
