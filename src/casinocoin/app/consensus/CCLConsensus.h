//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

//==============================================================================
/*
    2017-06-25  ajochems        Refactored for casinocoin
*/
//==============================================================================

#ifndef CASINOCOIN_APP_CONSENSUS_CCLCONSENSUS_H_INCLUDED
#define CASINOCOIN_APP_CONSENSUS_CCLCONSENSUS_H_INCLUDED

#include <BeastConfig.h>
#include <casinocoin/app/consensus/CCLCxLedger.h>
#include <casinocoin/app/consensus/CCLCxPeerPos.h>
#include <casinocoin/app/consensus/CCLCxTx.h>
#include <casinocoin/app/misc/FeeVote.h>
#include <casinocoin/basics/CountedObject.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/beast/utility/Journal.h>
#include <casinocoin/consensus/Consensus.h>
#include <casinocoin/core/JobQueue.h>
#include <casinocoin/overlay/Message.h>
#include <casinocoin/protocol/CasinocoinLedgerHash.h>
#include <casinocoin/protocol/STValidation.h>
#include <casinocoin/shamap/SHAMap.h>

namespace casinocoin {

class InboundTransactions;
class LocalTxs;
class LedgerMaster;

//! Types used to adapt consensus for CCL
struct CCLCxTraits
{
    //! Ledger type presented to Consensus
    using Ledger_t = CCLCxLedger;
    //! Peer identifier type used in Consensus
    using NodeID_t = NodeID;
    //! TxSet type presented to Consensus
    using TxSet_t = CCLTxSet;
};

/** Adapts the generic Consensus algorithm for use by CCL.

    @note The enabled_shared_from_this base allows the application to properly
    create a shared instance of CCLConsensus for use in the accept logic..
*/
class CCLConsensus final : public Consensus<CCLConsensus, CCLCxTraits>,
                           public std::enable_shared_from_this<CCLConsensus>,
                           public CountedObject<CCLConsensus>
{
    using Base = Consensus<CCLConsensus, CCLCxTraits>;

public:
    //! Constructor
    CCLConsensus(
        Application& app,
        std::unique_ptr<FeeVote>&& feeVote,
        LedgerMaster& ledgerMaster,
        LocalTxs& localTxs,
        InboundTransactions& inboundTransactions,
        typename Base::clock_type const& clock,
        beast::Journal journal);

    CCLConsensus(CCLConsensus const&) = delete;

    CCLConsensus&
    operator=(CCLConsensus const&) = delete;

    static char const*
    getCountedObjectName()
    {
        return "Consensus";
    }

    /** Save the given consensus proposed by a peer with nodeID for later
        use in consensus.

        @param peerPos Proposed peer position
        @param nodeID ID of peer
    */
    void
    storeProposal(CCLCxPeerPos::ref peerPos, NodeID const& nodeID);

    //! Whether we are validating consensus ledgers.
    bool
    validating() const
    {
        return validating_;
    }

    bool
    haveCorrectLCL() const
    {
        return mode() != Mode::wrongLedger;
    }

    bool
    proposing() const
    {
        return mode() == Mode::proposing;
    }

    /** Get the Json state of the consensus process.

        Called by the consensus_info RPC.

        @param full True if verbose response desired.
        @return     The Json state.
    */
    Json::Value
    getJson(bool full) const;

    //! See Consensus::startRound
    void
    startRound(
        NetClock::time_point const& now,
        CCLCxLedger::ID const& prevLgrId,
        CCLCxLedger const& prevLgr);

    //! See Consensus::timerEntry
    void
    timerEntry(NetClock::time_point const& now);

    //! See Consensus::gotTxSet
    void
    gotTxSet(NetClock::time_point const& now, CCLTxSet const& txSet);

    /** Returns validation public key */
    PublicKey const&
    getValidationPublicKey() const;

    /** Set validation private and public key pair. */
    void
    setValidationKeys(SecretKey const& valSecret, PublicKey const& valPublic);

private:
    friend class Consensus<CCLConsensus, CCLCxTraits>;

    //-------------------------------------------------------------------------
    // Consensus type requirements.

    /** Attempt to acquire a specific ledger.

        If not available, asynchronously acquires from the network.

        @param ledger The ID/hash of the ledger acquire
        @return Optional ledger, will be seated if we locally had the ledger
     */
    boost::optional<CCLCxLedger>
    acquireLedger(LedgerHash const& ledger);

    /** Get peers' proposed positions.
        @param prevLedger The base ledger which proposals are based on
        @return The set of proposals
    */
    std::vector<CCLCxPeerPos>
    proposals(LedgerHash const& prevLedger);

    /** Relay the given proposal to all peers

        @param peerPos The peer position to relay.
     */
    void
    relay(CCLCxPeerPos const& peerPos);

    /** Relay disputed transacction to peers.

        Only relay if the provided transaction hasn't been shared recently.

        @param tx The disputed transaction to relay.
    */
    void
    relay(CCLCxTx const& tx);

    /** Acquire the transaction set associated with a proposal.

        If the transaction set is not available locally, will attempt acquire it
        from the network.

        @param setId The transaction set ID associated with the proposal
        @return Optional set of transactions, seated if available.
   */
    boost::optional<CCLTxSet>
    acquireTxSet(CCLTxSet::ID const& setId);

    /** Whether the open ledger has any transactions
     */
    bool
    hasOpenTransactions() const;

    /** Number of proposers that have vallidated the given ledger

        @param h The hash of the ledger of interest
        @return the number of proposers that validated a ledger
    */
    std::size_t
    proposersValidated(LedgerHash const& h) const;

    /** Number of proposers that have validated a ledger descended from
       requested ledger.

        @param h The hash of the ledger of interest.
        @return The number of validating peers that have validated a ledger
                succeeding the one provided.
    */
    std::size_t
    proposersFinished(LedgerHash const& h) const;

    /** Propose the given position to my peers.

        @param proposal Our proposed position
    */
    void
    propose(CCLCxPeerPos::Proposal const& proposal);

    /** Relay the given tx set to peers.

        @param set The TxSet to share.
    */
    void
    relay(CCLTxSet const& set);

    /** Get the ID of the previous ledger/last closed ledger(LCL) on the network

        @param ledgerID ID of previous ledger used by consensus
        @param ledger Previous ledger consensus has available
        @param mode Current consensus mode
        @return The id of the last closed network

        @note ledgerID may not match ledger.id() if we haven't acquired
              the ledger matching ledgerID from the network
     */
    uint256
    getPrevLedger(
        uint256 ledgerID,
        CCLCxLedger const& ledger,
        Mode mode);

    /** Close the open ledger and return initial consensus position.

       @param ledger the ledger we are changing to
       @param closeTime When consensus closed the ledger
       @param mode Current consensus mode
       @return Tentative consensus result
    */
    Result
    onClose(
        CCLCxLedger const& ledger,
        NetClock::time_point const& closeTime,
        Mode mode);

    /** Process the accepted ledger.

        Accepting a ledger may be expensive, so this function can dispatch
        that call to another thread if desired.

        @param result The result of consensus
        @param prevLedger The closed ledger consensus worked from
        @param closeResolution The resolution used in agreeing on an effective
                               closeTiem
        @param rawCloseTimes The unrounded closetimes of ourself and our peers
        @param mode Our participating mode at the time consensus was declared
    */
    void
    onAccept(
        Result const& result,
        CCLCxLedger const& prevLedger,
        NetClock::duration const & closeResolution,
        CloseTimes const& rawCloseTimes,
        Mode const& mode);

    /** Process the accepted ledger that was a result of simulation/force
       accept.

        @ref onAccept
    */
    void
    onForceAccept(
        Result const& result,
        CCLCxLedger const& prevLedger,
        NetClock::duration const &closeResolution,
        CloseTimes const& rawCloseTimes,
        Mode const& mode);

    //!-------------------------------------------------------------------------
    // Additional members (not directly required by Consensus interface)
    /** Notify peers of a consensus state change

        @param ne Event type for notification
        @param ledger The ledger at the time of the state change
        @param haveCorrectLCL Whether we believ we have the correct LCL.
    */
    void
    notify(
        protocol::NodeEvent ne,
        CCLCxLedger const& ledger,
        bool haveCorrectLCL);

    /** Accept a new ledger based on the given transactions.

        @ref onAccept
     */
    void
    doAccept(
        Result const& result,
        CCLCxLedger const& prevLedger,
        NetClock::duration closeResolution,
        CloseTimes const& rawCloseTimes,
        Mode const& mode);

    /** Build the new last closed ledger.

        Accept the given the provided set of consensus transactions and build
        the last closed ledger. Since consensus just agrees on which
        transactions to apply, but not whether they make it into the closed
        ledger, this function also populates retriableTxs with those that can
        be retried in the next round.

        @param previousLedger Prior ledger building upon
        @param set The set of transactions to apply to the ledger
        @param closeTime The the ledger closed
        @param closeTimeCorrect Whether consensus agreed on close time
        @param closeResolution Resolution used to determine consensus close time
        @param roundTime Duration of this consensus rorund
        @param retriableTxs Populate with transactions to retry in next round
        @return The newly built ledger
  */
    CCLCxLedger
    buildLCL(
        CCLCxLedger const& previousLedger,
        CCLTxSet const& set,
        NetClock::time_point closeTime,
        bool closeTimeCorrect,
        NetClock::duration closeResolution,
        std::chrono::milliseconds roundTime,
        CanonicalTXSet& retriableTxs);

    /** Validate the given ledger and share with peers as necessary

        @param ledger The ledger to validate
        @param proposing Whether we were proposing transactions while generating
                         this ledger.  If we are not proposing, a validation
                         can still be sent to inform peers that we know we
                         aren't fully participating in consensus but are still
                         around and trying to catch up.
    */
    void
    validate(CCLCxLedger const& ledger, bool proposing);

    //!-------------------------------------------------------------------------
    Application& app_;
    std::unique_ptr<FeeVote> feeVote_;
    LedgerMaster& ledgerMaster_;
    LocalTxs& localTxs_;
    InboundTransactions& inboundTransactions_;
    beast::Journal j_;

    NodeID nodeID_;
    PublicKey valPublic_;
    SecretKey valSecret_;
    LedgerHash acquiringLedger_;

    // The timestamp of the last validation we used, in network time. This is
    // only used for our own validations.
    NetClock::time_point lastValidationTime_;

    using PeerPositions = hash_map<NodeID, std::deque<CCLCxPeerPos::pointer>>;
    PeerPositions peerPositions_;
    std::mutex peerPositionsLock_;

    bool validating_ = false;
    bool simulating_ = false;
};
}

#endif
