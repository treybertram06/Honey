#include "hnpch.h"
#include "layer_stack.h"

namespace Honey {
    LayerStack::LayerStack() {
        m_layer_insert = m_layers.begin();
    }

    LayerStack::~LayerStack() {
        for (Layer* layer : m_layers) {
            delete layer;
        }
    }

    void LayerStack::push_layer(Layer *layer) {
        m_layer_insert = m_layers.emplace(m_layer_insert, layer);
    }

    void LayerStack::push_overlay(Layer *overlay) {
        m_layers.emplace_back(overlay);
    }

    void LayerStack::pop_layer(Layer *layer) {
        auto it = std::find(m_layers.begin(), m_layers.end(), layer);
        if (it != m_layers.end()) {
            m_layers.erase(it);
            m_layer_insert--;
        }
    }

    void LayerStack::pop_overlay(Layer *overlay) {
        auto it = std::find(m_layers.begin(), m_layers.end(), overlay);
        if (it != m_layers.end()) {
            m_layers.erase(it);
        }

    }






}