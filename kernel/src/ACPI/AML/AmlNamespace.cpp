/*
    * AmlNamespace.cpp
    * ACPI namespace tree implementation
    * Copyright (c) 2026 Daniel Hammer
*/

#include "AmlNamespace.hpp"
#include <Libraries/Memory.hpp>

namespace Hal {
    namespace AML {

        Namespace::Namespace() : m_nodeCount(0) {
            for (int i = 0; i < MaxNamespaceNodes; i++)
                m_nodes[i].Clear();

            // Create root node "\"
            int32_t root = AllocNode();
            m_nodes[root].Name[0] = '\\';
            m_nodes[root].Name[1] = '\0';
            m_nodes[root].ParentIndex = -1;
        }

        int32_t Namespace::AllocNode() {
            if (m_nodeCount >= MaxNamespaceNodes)
                return -1;
            int32_t idx = m_nodeCount++;
            m_nodes[idx].Clear();
            return idx;
        }

        bool Namespace::SegmentEqual(const char* a, const char* b) {
            for (int i = 0; i < MaxNameSegLen; i++) {
                char ca = a[i] ? a[i] : '_';
                char cb = b[i] ? b[i] : '_';
                if (ca != cb) return false;
            }
            return true;
        }

        void Namespace::PadSegment(const char* src, char* dst) {
            int i = 0;
            while (i < MaxNameSegLen && src[i] != '\0') {
                dst[i] = src[i];
                i++;
            }
            while (i < MaxNameSegLen) {
                dst[i] = '_';
                i++;
            }
            dst[MaxNameSegLen] = '\0';
        }

        int Namespace::ParsePath(const char* path, char segments[][MaxNameSegLen + 1], int maxSegments) {
            if (!path || !*path) return 0;

            const char* p = path;

            // Skip leading backslash (root prefix)
            if (*p == '\\') p++;

            int count = 0;
            while (*p && count < maxSegments) {
                // Skip dots (parent prefix / dual/multi name prefix separator)
                if (*p == '.') { p++; continue; }
                // Skip caret (parent prefix) — we don't handle relative paths here
                if (*p == '^') { p++; continue; }

                // Read up to 4 characters for a name segment
                int i = 0;
                while (i < MaxNameSegLen && *p && *p != '.' && *p != '\\') {
                    segments[count][i] = *p;
                    i++;
                    p++;
                }
                // Pad with underscores
                while (i < MaxNameSegLen) {
                    segments[count][i] = '_';
                    i++;
                }
                segments[count][MaxNameSegLen] = '\0';
                count++;
            }

            return count;
        }

        int32_t Namespace::FindChildByName(int32_t parentIndex, const char* seg) const {
            auto* parent = GetNode(parentIndex);
            if (!parent) return -1;

            for (int32_t i = 0; i < parent->ChildCount; i++) {
                int32_t ci = parent->ChildIndices[i];
                if (ci < 0 || ci >= m_nodeCount) continue;
                if (SegmentEqual(m_nodes[ci].Name, seg))
                    return ci;
            }
            return -1;
        }

        int32_t Namespace::CreateNode(const char* absolutePath) {
            char segments[MaxPathDepth][MaxNameSegLen + 1];
            int segCount = ParsePath(absolutePath, segments, MaxPathDepth);

            int32_t current = 0; // root
            for (int i = 0; i < segCount; i++) {
                int32_t child = FindChildByName(current, segments[i]);
                if (child < 0) {
                    // Create the node
                    child = AllocNode();
                    if (child < 0) return -1;

                    memcpy(m_nodes[child].Name, segments[i], MaxNameSegLen + 1);
                    m_nodes[child].ParentIndex = current;

                    // Add to parent's children
                    auto* parent = &m_nodes[current];
                    if (parent->ChildCount < MaxChildren) {
                        parent->ChildIndices[parent->ChildCount++] = child;
                    } else {
                        return -1; // too many children
                    }
                }
                current = child;
            }
            return current;
        }

        int32_t Namespace::FindNode(const char* absolutePath) const {
            char segments[MaxPathDepth][MaxNameSegLen + 1];
            int segCount = ParsePath(absolutePath, segments, MaxPathDepth);

            int32_t current = 0; // root
            for (int i = 0; i < segCount; i++) {
                current = FindChildByName(current, segments[i]);
                if (current < 0) return -1;
            }
            return current;
        }

        int32_t Namespace::ResolveName(const char* name, int32_t scopeNodeIndex) const {
            // If it starts with '\', it's absolute
            if (name[0] == '\\')
                return FindNode(name);

            // Try to find relative to the current scope, walking up
            char padded[MaxNameSegLen + 1];
            PadSegment(name, padded);

            int32_t scope = scopeNodeIndex;
            while (scope >= 0) {
                int32_t found = FindChildByName(scope, padded);
                if (found >= 0) return found;
                scope = m_nodes[scope].ParentIndex;
            }

            return -1;
        }

        NamespaceNode* Namespace::GetNode(int32_t index) {
            if (index < 0 || index >= m_nodeCount) return nullptr;
            return &m_nodes[index];
        }

        const NamespaceNode* Namespace::GetNode(int32_t index) const {
            if (index < 0 || index >= m_nodeCount) return nullptr;
            return &m_nodes[index];
        }

        char* Namespace::GetNodePath(int32_t index, char* outBuf, int maxLen) const {
            if (!outBuf || maxLen < 2) return outBuf;

            // Build path by walking up to root
            // Collect segments in reverse
            char segments[MaxPathDepth][MaxNameSegLen + 1];
            int segCount = 0;

            int32_t cur = index;
            while (cur > 0 && segCount < MaxPathDepth) { // stop at root (index 0)
                auto* node = GetNode(cur);
                if (!node) break;
                memcpy(segments[segCount], node->Name, MaxNameSegLen + 1);
                segCount++;
                cur = node->ParentIndex;
            }

            // Write root prefix
            int pos = 0;
            outBuf[pos++] = '\\';

            // Write segments in reverse order
            for (int i = segCount - 1; i >= 0 && pos < maxLen - 1; i--) {
                if (i < segCount - 1 && pos < maxLen - 1)
                    outBuf[pos++] = '.';
                for (int j = 0; j < MaxNameSegLen && pos < maxLen - 1; j++)
                    outBuf[pos++] = segments[i][j];
            }

            outBuf[pos] = '\0';
            return outBuf;
        }

    };
};
