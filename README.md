# NETLINK_CONNECTOR 协议的思考

   NETLINK_CONNECTOR 是AF_NETLINK 协议族的中的一个内核与应用层通信的协议。 利用NETLINK_CONNECTOR可以方便的实现自定义的应用内核通信通道。但使用过程中会有一些不符合常规的语义问题。

## 问题分析
  以内核源码samples/connector 目录下的例子为例说明。 （对ucon.c代码稍有修改）
  执行./ucon总是成功，而不管内核模块是否加载或者卸载。
  1. 关于bind/connect操作： 
        代码片段如下：
```
         s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
        if (s == -1) {
                perror("socket");
                goto end;
        }

        l_local.nl_family = AF_NETLINK;
        l_local.nl_groups = (1 << (CN_TEST_IDX-1));     /* bitmask of requested groups */
        l_local.nl_pid = getpid();

        ulog("subscribing to %u.%u\n", CN_TEST_IDX, CN_TEST_VAL);

        if (bind(s, (struct sockaddr *)&l_local, sizeof(struct sockaddr_nl)) ==
            -1) {
                perror("bind");
                close(s);
                s = -1;
                goto end;
        }
```
        代码中的bind操作，不管内核中是否有CN_TEST_IDX 总是执行成功。 这会造成以下问题： 浪费资源，造成误解，也不符合常理。 
        
        bind的问题，也存在connect操作中。
        
    2. 关于send操作:
```
        err = send(s, nlh, size, 0);
        if (err == -1)
                ulog("Failed to send: %s [%d].\n",
                        strerror(errno), errno);
```
        不管内核中是否有CN_TEST_IDX，send操作总是执行成功，而且返回是数据的长度。这明显是错误的。
   
## 语义修改建议
     1. bind/connect的修改。 如果内核中CN_TEST_IDX不存在时，返回-1, 设置 NODEV。
     2. send的修改。 如果内核中CN_TEST_IDX不存在时，返回-1, 设置 NODEV。这也符合send的语义。
     
  
## 代码根源与修改
     1. bind问题
        drivers/connector/connector.c中
```
        struct netlink_kernel_cfg cfg = {
                .groups = CN_NETLINK_USERS + 0xf,
                .input  = cn_rx_skb,
        };
```
        在上面的结构中没有实现bind函数。修改如下：
```
        struct netlink_kernel_cfg cfg = {
                .groups = CN_NETLINK_USERS + 0xf,
                .input  = cn_rx_skb,
                .bind   = cn_bind,
        };
```
     
      2. send问题
         netlink_kernel_cfg 中函数  void (*input)(struct sk_buff *skb); 返回值是void, 改成len， 代表发送成功的数据的长度。
         并修改 文件中 net/netlink/af_netlink.c 的函数 netlink_unicast_kernel 代码：
```
         if (nlk->netlink_rcv != NULL) {
                len = skb->len;
                netlink_skb_set_owner_r(skb, sk);
                NETLINK_CB(skb).sk = ssk;
                netlink_deliver_tap_kernel(sk, ssk, skb);
                ret = nlk->netlink_rcv(skb);
                consume_skb(skb);
                if (ret >= 0)
                        ret = len;
          } else {
                kfree_skb(skb);
         }
```

      3. connect问题
         修改netlink_connect函数，增加 netlink_bind 类似逻辑，判断请求组是否存在。
```
         groups = nladdr->nl_groups;
         if (nlk->netlink_bind && groups) {
                err = nlk->netlink_bind(net, ffs(nladdr->nl_groups));
                if (err && nlk->netlink_unbind) {
                        nlk->netlink_unbind(net, ffs(nladdr->nl_groups));
                        goto end;
                }
         }
```

## 其他代码修改
       由于改变了 netlink_kernel_cfg 结构，相应的修改其他地方使用该结构的代码，使其符合新语义。
       
## 测试
       修改后测试bind/connect，如果相应的组不存在，均返回-ENODEV。 当组所在内核模块卸载时，send也会返回-ENODEV。
    
## 其他问题
       当组所在内核模块卸载时， read、poll/epoll则不会返回-ENODEV，也需要修改。
       但好像语义也可以接受，也有其他手段检测：相应模块退出时，可以通过发送-EPIPE告知应用，内核将要退出。 影响也不是很大。暂不修改。



