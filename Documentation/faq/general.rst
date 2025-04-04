..
      Licensed under the Apache License, Version 2.0 (the "License"); you may
      not use this file except in compliance with the License. You may obtain
      a copy of the License at

          http://www.apache.org/licenses/LICENSE-2.0

      Unless required by applicable law or agreed to in writing, software
      distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
      WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
      License for the specific language governing permissions and limitations
      under the License.

      Convention for heading levels in OVN documentation:

      =======  Heading 0 (reserved for the title in a document)
      -------  Heading 1
      ~~~~~~~  Heading 2
      +++++++  Heading 3
      '''''''  Heading 4

      Avoid deeper levels because they do not render well.

=======
General
=======

Q: What is OVN?

    A: OVN, the Open Virtual Network, is a system to support virtual network
    abstraction.  OVN complements the existing capabilities of OVS to add
    native support for virtual network abstractions, such as virtual L2 and L3
    overlays and security groups.

    OVN is intended to be used by cloud management software (CMS).
    For details about the architecture of OVN, see the ovn-architecture
    manpage. Some high-level features offered by OVN include

        * Distributed virtual routers
        * Distributed logical switches
        * Access Control Lists
        * DHCP
        * DNS server

Q: How can I try OVN?

    A: The OVN source code can be built on a Linux system.  You can
    build and experiment with OVN on any Linux machine.  Packages for
    various Linux distributions are available on many platforms, including:
    Debian, Ubuntu, Fedora.

Q: Why does OVN use Geneve instead of VLANs or VXLAN (or GRE)?

    A: OVN implements a fairly sophisticated packet processing pipeline in
    "logical datapaths" that can implement switching or routing functionality.
    A logical datapath has an ingress pipeline and an egress pipeline, and each
    of these pipelines can include logic based on packet fields as well as
    packet metadata such as the logical ingress and egress ports (the latter
    only in the egress pipeline).

    The processing for a logical datapath can be split across hypervisors.  In
    particular, when a logical ingress pipeline executes an "output" action,
    OVN passes the packet to the egress pipeline on the hypervisor (or, in the
    case of output to a logical multicast group, hypervisors) on which the
    logical egress port is located.  If this hypervisor is not the same as the
    ingress hypervisor, then the packet has to be transmitted across a physical
    network.

    This situation is where tunneling comes in.  To send the packet to another
    hypervisor, OVN encapsulates it with a tunnel protocol and sends the
    encapsulated packet across the physical network.  When the remote
    hypervisor receives the tunnel packet, it decapsulates it and passes it
    through the logical egress pipeline.  To do so, it also needs the metadata,
    that is, the logical ingress and egress ports.

    Thus, to implement OVN logical packet processing, at least the following
    metadata must pass across the physical network:

    * Logical datapath ID, a 24-bit identifier.  In Geneve, OVN uses the VNI to
      hold the logical datapath ID.

    * Logical ingress port, a 15-bit identifier.  In Geneve, OVN uses an option
      to hold the logical ingress port.

    * Logical egress port, a 16-bit identifier.  In Geneve, OVN uses an option
      to hold the logical egress port.

    See ``ovn-architecture(7)``, under "Tunnel Encapsulations", for details.

    Together, these metadata require 24 + 15 + 16 = 55 bits.  GRE provides 32
    bits, VXLAN provides 24, and VLAN only provides 12.  Most notably, if
    logical egress pipelines do not match on the logical ingress port, thereby
    restricting the class of ACLs available to users, then this eliminates 15
    bits, bringing the requirement down to 40 bits.  At this point, one can
    choose to limit the size of the OVN logical network in various ways, e.g.:

    * 16 bits of logical datapaths + 16 bits of logical egress ports.  This
      combination fits within a 32-bit GRE tunnel key.

    * 12 bits of logical datapaths + 12 bits of logical egress ports.  This
      combination fits within a 24-bit VXLAN VNI.

    * It's difficult to identify an acceptable compromise for a VLAN-based
      deployment.

    These compromises wouldn't suit every site, since some deployments
    may need to allocate more bits to the datapath or egress port
    identifiers.

    As a side note, OVN does support VXLAN for use with ASIC-based top of rack
    switches, using ``ovn-controller-vtep(8)`` and the OVSDB VTEP schema
    described in ``vtep(5)``, but this limits the features available from OVN
    to the subset available from the VTEP schema.

Q: How can I contribute to the OVN Community?

    A: You can start by joining the mailing lists and helping to answer
    questions.  You can also suggest improvements to documentation.  If you
    have a feature or bug you would like to work on, send a mail to one of the
    :doc:`mailing lists </internals/mailing-lists>`.

Q: What does it mean when a feature is marked "experimental"?

    A: Experimental features are marked this way because of one of
    several reasons:

    * The developer was only able to test the feature in a limited
      environment. Therefore the feature may not always work as intended
      in all environments.

    * During review, the potential for failure was noticed, but the
      circumstances that would lead to that failure were hard to nail
      down or were strictly theoretical.

    * What exists in OVN may be an early version of a more fleshed-out
      feature to come in a later version.

    * The feature was developed against a draft RFC that is subject to
      change when the RFC is published.

    * The feature was developed based on observations of how a specific
      vendor implements a feature, rather than using IETF standards or
      other documentated specifications.

    A feature may be declared experimental for other reasons as well,
    but the above are the most common. When a feature is marked
    experimental, it has the following properties:

    * The feature must be opt-in. The feature must be disabled by
      default. When the feature is disabled, it must have no bearing
      on other OVN functionality.

    * Configuration and implementation details of the feature are
      subject to change between major or minor versions of OVN.

    * Users make use of this feature at their own risk. Users are free
      to file issues against the feature, but developers are more likely
      to prioritize work on non-experimental features first.

    * Experimental features may be removed. For instance, if an
      experimental feature exposes a security risk, it may be removed
      rather than repaired.

    The hope is that experimental features will eventually lose the
    "experimental" marker and become a core feature. However, there is
    no specific test or process defined for when a feature no longer
    needs to be considered experimental. This typically will be decided
    collectively by OVN maintainers.

Q: How is a feature marked "experimental"?

    A: Experimental features must contain the following note in their man
    pages (ovn-nb.5, ovn-sb.5, ovn-controller.8, etc): "NOTE: this feature
    is experimental and may be subject to removal/change in the future.:
