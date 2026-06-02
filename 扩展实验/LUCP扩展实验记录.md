# LUCP扩展实验进度

- [x] 信令包ACL处理

- [x] 长距时延模拟
  - [x] 额外ACL重定向反向UDP dstPort 4791报文
  
  - [x] 在外挂主机上延迟发送
  
- [x] 瓶颈共享实验设计

  `switch1.conf`

  ```
  [Mac]
  b0:51:8e:f6:35:2f
  [Port]
  Ethernet48 95.0
  Ethernet49 95.0
  Ethernet50 95.0
  Ethernet4 95.0
  [Route]
  10.2.229.111 Ethernet48
  10.2.229.121 Ethernet49
  10.2.229.131 Ethernet50
  10.2.152.211 Ethernet4
  10.2.152.221 Ethernet4
  10.2.152.231 Ethernet4
  ```

  `local.conf`

  ```
  # Configuration File
  RdmaGidIndex = 3
  ListenPort = 52025
  MaxThreadNum = 16
  DefaultRate = 100
  BlockSize = 4 / 512
  BlockNum = 4096
  SavedFolderPath = /root
  # End of Configuration File
  ```

  `master.conf`

  ```
  10.2.152.231 10.2.152.231 10.2.229.131 52025 60G 0s
  10.2.152.221 10.2.152.221 10.2.229.131 52025 40G 1s
  10.2.152.211 10.2.152.211 10.2.229.111 52025 20G 2s
  ```

  
| 3ms RTT               | DCQCN                                        | LUCP                                        |
| :-------------------- | -------------------------------------------- | ------------------------------------------- |
| PFC frame             | 1,453,516                                    | 820                                         |
| PFC time              | 3,572,819                                    | 2502 unit                                   |
| 链路平均利用率        | 89.76Gpbs                                    | 94.33Gbps                                   |
| 总完成时间            | 11.44s                                       | 10.89s                                      |
| Jain’s Fairness Index | 0.991                                        | 0.999                                       |
| 实验图                | ![](./LUCP扩展实验记录/rate_dcqcn_expt1.svg) | ![](./LUCP扩展实验记录/rate_lucp_expt1.svg) |
- [x] 瓶颈区分实验设计

  `switch1.conf`

  ```
  [Mac]
  b0:51:8e:f6:35:2f
  [Port]
  Ethernet48 38.0
  Ethernet49 38.0
  Ethernet50 38.0
  Ethernet4 95.0
  [Route]
  10.2.229.111 Ethernet48
  10.2.229.121 Ethernet49
  10.2.229.131 Ethernet50
  10.2.152.211 Ethernet4
  10.2.152.221 Ethernet4
  10.2.152.231 Ethernet4
  ```

  `local.conf`

  ```
  # Configuration File
  RdmaGidIndex = 3
  ListenPort = 52025
  MaxThreadNum = 16
  DefaultRate = 100
  BlockSize = 4 / 256
  BlockNum = 4096
  SavedFolderPath = /root
  # End of Configuration File
  ```

  `master.conf`

  ```
  10.2.152.231 10.2.152.231 10.2.229.131 52025 60G 0s
  10.2.152.221 10.2.152.221 10.2.229.131 52025 40G 1s
  10.2.152.211 10.2.152.211 10.2.229.111 52025 20G 2s
  ```

  
| 3ms RTT               | DCQCN                                        | LUCP                                        |
| :-------------------- | -------------------------------------------- | ------------------------------------------- |
| 总完成时间            | 23.06s                                       | 22.63s                                      |
| Jain’s Fairness Index | 0.703                                        | 0.972                                       |
| 实验图                | ![](./LUCP扩展实验记录/rate_dcqcn_expt2.svg) | ![](./LUCP扩展实验记录/rate_lucp_expt2.svg) |
- [ ] 背景流实验设计与代码实现

```mermaid
graph TB
    %% 定义节点样式
    classDef server fill:#f9f,stroke:#333,stroke-width:2px,rx:5,ry:5;
    classDef switch fill:#bef,stroke:#333,stroke-width:2px,rx:10,ry:10;
    classDef invisible opacity:0,height:0px,width:0px;

    subgraph Main_Layout [交换网络拓扑]
        direction LR

        %% 中间层：包含发送侧、核心交换机、接收侧
            
            %% 左侧：接收侧
            subgraph Left_Side [接收侧]
                direction LR
                S1["Server 1<br/>IP_ADDR1"]:::server
                S2["Server 2<br/>IP_ADDR2"]:::server
            end

            %% 中间：Switch A 和 B
            subgraph Core_Network [核心交换]
                    %% 顶层：Sw1 和 Sw2
                    direction LR
         
        subgraph Top_Switches [管理/汇聚层]
            direction TB
            Sw1((Sw1)):::server
            Sw2((Sw2)):::server
        end
          subgraph Middle_Row [核心转发层]
                direction TB
                SwA((Switch A)):::switch
                SwB((Switch B)):::switch
                SwA ===|100G| SwB

            end

            %% 右侧：发送侧
            subgraph Right_Side [发送侧]
                direction LR
                S3["Server 3<br/>IP_ADDR3"]:::server
                S4["Server 4<br/>IP_ADDR4"]:::server
                S5["Server 5<br/>IP_ADDR5"]:::server
                S6["Server 6<br/>IP_ADDR6"]:::server
            end
        end
    end

    %% 垂直连接关系
    Sw1 === SwA
    Sw2 === SwB

    %% 水平逻辑连接
    S1 ===|100G| SwA
    S2 ===|100G| SwA
    
    SwB ===|100G| S3
    SwB ===|100G| S4
    SwB ===|100G| S5
    SwB ===|100G| S6
```

