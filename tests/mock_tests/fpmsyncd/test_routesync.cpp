#include "redisutility.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mock_table.h"
#define private public
#include "fpmsyncd/routesync.h"
#undef private

#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <netlink/route/link.h>
#include <netlink/route/nexthop.h>
#include <linux/nexthop.h>

#include <sstream>

using namespace swss;
#define MAX_PAYLOAD 1024

using ::testing::_;

class MockRouteSync : public RouteSync
{
public:
    MockRouteSync(RedisPipeline *m_pipeline) : RouteSync(m_pipeline)
    {
    }

    ~MockRouteSync()
    {
    }
    MOCK_METHOD(bool, getEvpnNextHop, (nlmsghdr *, int,
                               rtattr *[], std::string&,
                               std::string& , std::string&,
                               std::string&), (override));
    MOCK_METHOD(bool, getIfName, (int, char *, size_t), (override));
    MOCK_METHOD(void, onNextHopMsg, (struct nlmsghdr *, int), (override));
};
class MockFpm : public FpmInterface
{
public:
    MockFpm(RouteSync* routeSync) :
        m_routeSync(routeSync)
    {
        m_routeSync->onFpmConnected(*this);
    }

    ~MockFpm() override
    {
        m_routeSync->onFpmDisconnected();
    }

    MOCK_METHOD1(send, bool(nlmsghdr*));
    MOCK_METHOD0(getFd, int());
    MOCK_METHOD0(readData, uint64_t());

private:
    RouteSync* m_routeSync{};
};

class FpmSyncdResponseTest : public ::testing::Test
{
public:
    void SetUp() override
    {
        EXPECT_EQ(rtnl_route_read_protocol_names(DefaultRtProtoPath), 0);
        m_routeSync.setSuppressionEnabled(true);
    }

    void TearDown() override
    {
    }

    shared_ptr<swss::DBConnector> m_db = make_shared<swss::DBConnector>("APPL_DB", 0);
    shared_ptr<RedisPipeline> m_pipeline = make_shared<RedisPipeline>(m_db.get());
    RouteSync m_routeSync{m_pipeline.get()};
    MockFpm m_mockFpm{&m_routeSync};
    MockRouteSync m_mockRouteSync{m_pipeline.get()};
};

TEST_F(FpmSyncdResponseTest, RouteResponseFeedbackV4)
{
    // Expect the message to zebra is sent
    EXPECT_CALL(m_mockFpm, send(_)).WillOnce([&](nlmsghdr* hdr) -> bool {
        rtnl_route* routeObject{};

        rtnl_route_parse(hdr, &routeObject);

        // table is 0 when no in default VRF
        EXPECT_EQ(rtnl_route_get_table(routeObject), 0);
        EXPECT_EQ(rtnl_route_get_protocol(routeObject), RTPROT_KERNEL);

        // Offload flag is set
        EXPECT_EQ(rtnl_route_get_flags(routeObject) & RTM_F_OFFLOAD, RTM_F_OFFLOAD);

        return true;
    });

    m_routeSync.onRouteResponse("1.0.0.0/24", {
        {"err_str", "SWSS_RC_SUCCESS"},
        {"protocol", "kernel"},
    });
}

TEST_F(FpmSyncdResponseTest, RouteResponseFeedbackV4Vrf)
{
    // Expect the message to zebra is sent
    EXPECT_CALL(m_mockFpm, send(_)).WillOnce([&](nlmsghdr* hdr) -> bool {
        rtnl_route* routeObject{};

        rtnl_route_parse(hdr, &routeObject);

        // table is 42 (returned by fake link cache) when in non default VRF
        EXPECT_EQ(rtnl_route_get_table(routeObject), 42);
        EXPECT_EQ(rtnl_route_get_protocol(routeObject), 200);

        // Offload flag is set
        EXPECT_EQ(rtnl_route_get_flags(routeObject) & RTM_F_OFFLOAD, RTM_F_OFFLOAD);

        return true;
    });

    m_routeSync.onRouteResponse("Vrf0:1.0.0.0/24", {
        {"err_str", "SWSS_RC_SUCCESS"},
        {"protocol", "200"},
    });
}

TEST_F(FpmSyncdResponseTest, RouteResponseFeedbackV6)
{
    // Expect the message to zebra is sent
    EXPECT_CALL(m_mockFpm, send(_)).WillOnce([&](nlmsghdr* hdr) -> bool {
        rtnl_route* routeObject{};

        rtnl_route_parse(hdr, &routeObject);

        // table is 0 when no in default VRF
        EXPECT_EQ(rtnl_route_get_table(routeObject), 0);
        EXPECT_EQ(rtnl_route_get_protocol(routeObject), RTPROT_KERNEL);

        // Offload flag is set
        EXPECT_EQ(rtnl_route_get_flags(routeObject) & RTM_F_OFFLOAD, RTM_F_OFFLOAD);

        return true;
    });

    m_routeSync.onRouteResponse("1::/64", {
        {"err_str", "SWSS_RC_SUCCESS"},
        {"protocol", "kernel"},
    });
}

TEST_F(FpmSyncdResponseTest, RouteResponseFeedbackV6Vrf)
{
    // Expect the message to zebra is sent
    EXPECT_CALL(m_mockFpm, send(_)).WillOnce([&](nlmsghdr* hdr) -> bool {
        rtnl_route* routeObject{};

        rtnl_route_parse(hdr, &routeObject);

        // table is 42 (returned by fake link cache) when in non default VRF
        EXPECT_EQ(rtnl_route_get_table(routeObject), 42);
        EXPECT_EQ(rtnl_route_get_protocol(routeObject), 200);

        // Offload flag is set
        EXPECT_EQ(rtnl_route_get_flags(routeObject) & RTM_F_OFFLOAD, RTM_F_OFFLOAD);

        return true;
    });

    m_routeSync.onRouteResponse("Vrf0:1::/64", {
        {"err_str", "SWSS_RC_SUCCESS"},
        {"protocol", "200"},
    });
}

TEST_F(FpmSyncdResponseTest, WarmRestart)
{
    std::vector<FieldValueTuple> fieldValues = {
        {"protocol", "kernel"},
    };

    DBConnector applStateDb{"APPL_STATE_DB", 0};
    Table routeStateTable{&applStateDb, APP_ROUTE_TABLE_NAME};

    routeStateTable.set("1.0.0.0/24", fieldValues);
    routeStateTable.set("2.0.0.0/24", fieldValues);
    routeStateTable.set("Vrf0:3.0.0.0/24", fieldValues);

    EXPECT_CALL(m_mockFpm, send(_)).Times(3).WillRepeatedly([&](nlmsghdr* hdr) -> bool {
        rtnl_route* routeObject{};

        rtnl_route_parse(hdr, &routeObject);

        // Offload flag is set
        EXPECT_EQ(rtnl_route_get_flags(routeObject) & RTM_F_OFFLOAD, RTM_F_OFFLOAD);

        return true;
    });

    m_routeSync.onWarmStartEnd(applStateDb);
}

TEST_F(FpmSyncdResponseTest, testEvpn)
{
    struct nlmsghdr *nlh = (struct nlmsghdr *) malloc(NLMSG_SPACE(MAX_PAYLOAD));
    shared_ptr<swss::DBConnector> m_app_db;
    m_app_db = make_shared<swss::DBConnector>("APPL_DB", 0);
    Table app_route_table(m_app_db.get(), APP_ROUTE_TABLE_NAME);

    memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
    nlh->nlmsg_type = RTM_NEWROUTE;
    struct rtmsg rtm;
    rtm.rtm_family = AF_INET;
    rtm.rtm_protocol = 200;
    rtm.rtm_type = RTN_UNICAST;
    rtm.rtm_table = 0;
    rtm.rtm_dst_len = 32;
    nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
    memcpy(NLMSG_DATA(nlh), &rtm, sizeof(rtm));

    EXPECT_CALL(m_mockRouteSync, getEvpnNextHop(_, _, _, _, _, _, _)).Times(testing::AtLeast(1)).WillOnce([&](
                               struct nlmsghdr *h, int received_bytes,
                               struct rtattr *tb[], std::string& nexthops,
                               std::string& vni_list, std::string& mac_list,
                               std::string& intf_list)-> bool {
        vni_list="100";
        mac_list="aa:aa:aa:aa:aa:aa";
        intf_list="Ethernet0";
        nexthops = "1.1.1.1";
        return true;
    });
    m_mockRouteSync.onMsgRaw(nlh);
    
    vector<string> keys;
    vector<FieldValueTuple> fieldValues;
    app_route_table.getKeys(keys);
    ASSERT_EQ(keys.size(), 1);

    app_route_table.get(keys[0], fieldValues);
    auto value = swss::fvsGetValue(fieldValues, "protocol", true);
    ASSERT_EQ(value.get(), "0xc8");

}

TEST_F(FpmSyncdResponseTest, testNewNextHopMsg)
{
    struct nlmsghdr *nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));
    memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));
    nlh->nlmsg_type = RTM_NEWNEXTHOP;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE;

    struct nhmsg *nhm = (struct nhmsg *)NLMSG_DATA(nlh);
    nhm->nh_family = AF_INET;
    struct rtattr *rta = (struct rtattr *)((char *)nlh + NLMSG_LENGTH(sizeof(struct nhmsg)));

    uint32_t test_id = 10;
    const char* test_gateway = "192.168.1.1";
    int32_t test_ifindex = 5;

    rta->rta_type = NHA_ID;
    rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
    memcpy(RTA_DATA(rta), &test_id, sizeof(uint32_t));
    rta = (struct rtattr *)((char *)rta + RTA_ALIGN(rta->rta_len));

    struct in_addr gw_addr;
    inet_pton(AF_INET, test_gateway, &gw_addr);
    rta->rta_type = NHA_GATEWAY;
    rta->rta_len = RTA_LENGTH(sizeof(struct in_addr));
    memcpy(RTA_DATA(rta), &gw_addr, sizeof(struct in_addr));
    rta = (struct rtattr *)((char *)rta + RTA_ALIGN(rta->rta_len));

    rta->rta_type = NHA_OIF;
    rta->rta_len = RTA_LENGTH(sizeof(int32_t));
    memcpy(RTA_DATA(rta), &test_ifindex, sizeof(int32_t));
    rta = (struct rtattr *)((char *)rta + RTA_ALIGN(rta->rta_len));

    nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);

    int len = (int)(NLMSG_SPACE(MAX_PAYLOAD) - NLMSG_LENGTH(sizeof(struct nhmsg)));

    EXPECT_CALL(m_mockRouteSync, onNextHopMsg(nlh, len))
        .Times(1)
        .WillOnce([&](struct nlmsghdr *h, int actuallen)-> void {
            std::cout << "onNextHopMsg is invoked!" << std::endl;
            EXPECT_EQ(actuallen, len);
            return;
        });

    m_mockRouteSync.onMsgRaw(nlh);

    auto it = m_mockRouteSync.m_nh_groups.find(test_id);
    if (it == m_mockRouteSync.m_nh_groups.end())
    {
        std::cout << "fail to add new nh" << std::endl;
    }

    // EXPECT_EQ(it->second.id, test_id);
    // EXPECT_EQ(it->second.nexthop, "192.168.1.1");
    // EXPECT_EQ(it->second.intf, "Ethernet0");

    free(nlh);
}
